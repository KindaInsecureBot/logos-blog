#include "blog_plugin.h"
#include "module_proxy.h"

#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QTextStream>

BlogPlugin::BlogPlugin(QObject* parent)
    : QObject(parent)
    , m_posts(new PostStore(this))
    , m_feed(new FeedStore(this))
    , m_chatSync(new ChatSync(this))
    , m_rss(new RssServer(this))
{}

void BlogPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    m_api    = api;

    // ── kv_module ──────────────────────────────────────────────────────────
    m_kv = api->getClient("kv_module");
    if (m_kv) {
        m_kv->invokeRemoteMethod("kv_module", "setDataDir",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/blog-data");

        m_posts->setKvClient(m_kv);
        m_feed->setKvClient(m_kv);
    }

    // ── storage_module (org.logos.StorageModuleInterface) ──────────────────
    m_storage = api->getClient("storage_module");
    if (m_storage) {
        m_posts->setStorageClient(m_storage);
    }

    // ── chat_module (org.logos.ChatSDKModuleInterface) ─────────────────────
    m_chat = api->getClient("chat_module");
    if (m_chat) {
        m_chatSync->setChatClient(m_chat);
    }

    // ── lez_registry_module ────────────────────────────────────────────────
    m_lezRegistry = api->getClient("lez_registry_module");

    // ── PostStore → Chat + LEZ registry ───────────────────────────────────
    // When a post is published: upload the signed envelope to Storage, send a
    // compact CID notification via Chat, and inscribe the CID on-chain.
    connect(m_posts, &PostStore::postPublished,
            this, [this](const QString& id, const QString& storageCid) {
        // Fetch the full post content so we can build the signed envelope
        const QJsonObject post = m_posts->getPost(id);
        if (post.isEmpty()) return;

        QJsonObject payload;
        payload["post"] = post;
        if (!storageCid.isEmpty()) payload["cid"] = storageCid;
        const QString signedEnvelope = buildSignedEnvelope("post", payload);
        const QString toEmit = signedEnvelope.isEmpty()
            ? QString::fromUtf8(QJsonDocument(post).toJson(QJsonDocument::Compact))
            : signedEnvelope;

        emit postPublished(toEmit);

        // Send a compact CID notification via Chat SDK so subscribers can
        // discover the new post and fetch it from Storage
        if (m_chatSync && !storageCid.isEmpty()) {
            QJsonObject notif;
            notif["version"]   = 1;
            notif["type"]      = "post";
            notif["cid"]       = storageCid;
            QJsonObject author;
            author["pubkey"]   = m_ownPubkey;
            author["name"]     = m_displayName;
            notif["author"]    = author;
            notif["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            const QByteArray canonical =
                QJsonDocument(notif).toJson(QJsonDocument::Compact);
            const QString sig = Crypto::sign(m_ownPrivkey, canonical);
            if (!sig.isEmpty()) notif["signature"] = sig;
            m_chatSync->publishMessage(
                QString::fromUtf8(QJsonDocument(notif).toJson(QJsonDocument::Compact)));
        } else if (m_chatSync) {
            // No Storage CID: send the full signed envelope directly
            if (!signedEnvelope.isEmpty()) m_chatSync->publishMessage(signedEnvelope);
        }

        // Inscribe CID in the on-chain LEZ registry
        if (!storageCid.isEmpty()) inscribeInRegistry(storageCid);
    });

    // ── FeedStore → plugin signals ─────────────────────────────────────────
    connect(m_feed, &FeedStore::postIngested,
            this, [this](const QString& authorPubkey, const QString& postId) {
        const QJsonObject post = m_feed->getPost(authorPubkey, postId);
        emit postReceived(QString::fromUtf8(QJsonDocument(post).toJson(QJsonDocument::Compact)));
    });
    connect(m_feed, &FeedStore::postDeleted,
            this, &BlogPlugin::postDeleted);
    connect(m_feed, &FeedStore::profileUpdated,
            this, [this](const QString& pubkey, const QString& name) {
        QJsonObject profile;
        profile["pubkey"] = pubkey;
        profile["name"]   = name;
        emit profileUpdated(pubkey,
            QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact)));
    });

    // ── ChatSync → FeedStore ingestion ─────────────────────────────────────
    connect(m_chatSync, &ChatSync::messageReceived,
            this, [this](const QString& senderPubkey, const QString& envelopeJson) {
        // Subscription gate
        if (senderPubkey != m_ownPubkey && !m_feed->isSubscribed(senderPubkey)) return;

        const QJsonObject envelope =
            QJsonDocument::fromJson(envelopeJson.toUtf8()).object();
        if (envelope.isEmpty()) return;

        const QString type = envelope["type"].toString();

        if (type == "post") {
            const QString cid = envelope["cid"].toString();
            if (!cid.isEmpty() && m_storage) {
                // Fetch the full post content from Storage and reconstruct the
                // ingestible envelope so FeedStore can verify it normally
                const QString content = fetchFromStorage(cid);
                if (!content.isEmpty()) {
                    const QJsonObject fetchedPost =
                        QJsonDocument::fromJson(content.toUtf8()).object();
                    if (!fetchedPost.isEmpty()) {
                        QJsonObject fullEnvelope = envelope;
                        fullEnvelope["post"] = fetchedPost;
                        m_feed->ingestPost(fullEnvelope);
                    }
                }
            } else {
                // Envelope carries post content directly (no-storage path)
                m_feed->ingestPost(envelope);
            }
        } else if (type == "delete") {
            m_feed->ingestDelete(envelope);
        } else if (type == "profile") {
            m_feed->ingestProfile(envelope);
        }
    });

    connect(m_chatSync, &ChatSync::chatStarted, this, [this]() {
        emit chatStarted();
    });

    // Load or create identity (sets m_ownPubkey / m_ownPrivkey)
    loadOrCreateIdentity();

    // Connect chat_module event signal → ChatSync dispatcher
    connectChatModule();

    // Start RSS server
    startRssServer();

    // Start Chat node and re-subscribe to all saved author channels
    m_chatSync->start();
    for (const QString& pk : m_feed->subscribedPubkeys()) {
        m_chatSync->subscribeToAuthor(pk);
    }
}

void BlogPlugin::connectChatModule()
{
    if (!m_chat) return;

    // chat_module fires: messageReceived(moduleId, convoId, senderPubkey, contentHex)
    m_api->on("chat_module", "messageReceived", [this](QVariantList args) {
        // args: [moduleId, convoId, senderPubkey, contentHex]
        if (args.size() < 4) return;
        const QString convoId     = args.value(1).toString();
        const QString senderPubkey = args.value(2).toString();
        const QString contentHex  = args.value(3).toString();
        m_chatSync->onChatMessage(convoId, senderPubkey, contentHex);
    });
}

QString BlogPlugin::fetchFromStorage(const QString& cid)
{
    if (!m_storage || cid.isEmpty()) return {};
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "downloadChunks", cid);
    return result.toString();
}

void BlogPlugin::inscribeInRegistry(const QString& cid)
{
    if (!m_lezRegistry || m_ownPubkey.isEmpty() || cid.isEmpty()) return;
    m_lezRegistry->invokeRemoteMethod(
        "lez_registry_module", "register_post", m_ownPubkey, cid);
}

void BlogPlugin::loadOrCreateIdentity()
{
    if (!m_kv) return;

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                                               QString("blog"), QString("identity"));
    if (!result.toString().isEmpty()) {
        const QJsonObject identity = QJsonDocument::fromJson(
            result.toString().toUtf8()).object();
        const QString pubkey  = identity["pubkey"].toString();
        const QString privkey = identity["privkey"].toString();
        if (!pubkey.isEmpty() && !privkey.isEmpty()) {
            m_ownPubkey   = pubkey;
            m_ownPrivkey  = privkey;
            m_displayName = identity["display_name"].toString();
            if (m_chatSync) m_chatSync->setOwnPubkey(pubkey);
            return;
        }
    }

    const Keypair kp = Crypto::generateEd25519Keypair();
    if (kp.pubkeyHex.isEmpty()) return;

    QJsonObject identity;
    identity["pubkey"]       = kp.pubkeyHex;
    identity["privkey"]      = kp.privkeyHex;
    identity["display_name"] = "Logos User";
    identity["bio"]          = QString();
    identity["created_at"]   = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("identity"),
        QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));

    m_ownPubkey   = kp.pubkeyHex;
    m_ownPrivkey  = kp.privkeyHex;
    m_displayName = "Logos User";
    if (m_chatSync) m_chatSync->setOwnPubkey(m_ownPubkey);
    emit identityChanged();
}

void BlogPlugin::startRssServer()
{
    m_rss->setPostStore(m_posts);
    m_rss->setFeedStore(m_feed);

    int     port = 8484;
    QString bind = "127.0.0.1";

    if (m_kv) {
        QVariant pv = m_kv->invokeRemoteMethod("kv_module", "get",
                          QString("blog"), QString("settings:rss_port"));
        if (!pv.toString().isEmpty()) port = pv.toString().toInt();

        QVariant bv = m_kv->invokeRemoteMethod("kv_module", "get",
                          QString("blog"), QString("settings:rss_bind"));
        if (!bv.toString().isEmpty()) bind = bv.toString();
    }

    m_rss->start(bind, port);
}

QString BlogPlugin::buildSignedEnvelope(const QString& type,
                                        const QJsonObject& typePayload)
{
    if (m_ownPubkey.isEmpty() || m_ownPrivkey.isEmpty()) return {};

    QJsonObject author;
    author["pubkey"] = m_ownPubkey;
    author["name"]   = m_displayName;

    QJsonObject envelope;
    envelope["version"]   = 1;
    envelope["type"]      = type;
    envelope["author"]    = author;
    envelope["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    for (auto it = typePayload.begin(); it != typePayload.end(); ++it) {
        envelope[it.key()] = it.value();
    }

    const QByteArray canonical = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const QString sig = Crypto::sign(m_ownPrivkey, canonical);
    if (sig.isEmpty()) return {};

    envelope["signature"] = sig;
    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

// ── Identity ──────────────────────────────────────────────────────────────────

QString BlogPlugin::getIdentity()
{
    if (!m_kv) return "{}";
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                          QString("blog"), QString("identity"));
    if (result.toString().isEmpty()) return "{}";

    QJsonObject identity = QJsonDocument::fromJson(result.toString().toUtf8()).object();
    identity.remove("privkey");
    return QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

bool BlogPlugin::setIdentity(const QString& displayName, const QString& bio)
{
    if (!m_kv) return false;

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                          QString("blog"), QString("identity"));
    QJsonObject identity = result.toString().isEmpty()
        ? QJsonObject()
        : QJsonDocument::fromJson(result.toString().toUtf8()).object();

    identity["display_name"] = displayName;
    identity["bio"]          = bio;

    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("identity"),
        QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));

    m_displayName = displayName;
    emit identityChanged();

    // Publish updated profile via Chat SDK
    if (m_chatSync && !m_ownPubkey.isEmpty()) {
        QJsonObject profile;
        profile["name"]       = displayName;
        profile["bio"]        = bio;
        profile["avatar_url"] = QString();
        QJsonObject payload;
        payload["profile"] = profile;
        const QString envelope = buildSignedEnvelope("profile", payload);
        if (!envelope.isEmpty()) m_chatSync->publishMessage(envelope);
    }

    return true;
}

QString BlogPlugin::generateKeypair()
{
    const Keypair kp = Crypto::generateEd25519Keypair();
    if (kp.pubkeyHex.isEmpty()) return "{}";

    if (m_kv) {
        QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                              QString("blog"), QString("identity"));
        QJsonObject identity = result.toString().isEmpty()
            ? QJsonObject()
            : QJsonDocument::fromJson(result.toString().toUtf8()).object();

        identity["pubkey"]  = kp.pubkeyHex;
        identity["privkey"] = kp.privkeyHex;
        if (!identity.contains("created_at"))
            identity["created_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        m_kv->invokeRemoteMethod("kv_module", "set",
            QString("blog"), QString("identity"),
            QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));
    }

    m_ownPubkey  = kp.pubkeyHex;
    m_ownPrivkey = kp.privkeyHex;
    if (m_chatSync) m_chatSync->setOwnPubkey(m_ownPubkey);
    emit identityChanged();

    QJsonObject out;
    out["pubkey"] = kp.pubkeyHex;
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

// ── Post management ───────────────────────────────────────────────────────────

QString BlogPlugin::createPost(const QString& title, const QString& body,
                               const QString& summary, const QStringList& tags)
{
    return m_posts->createDraft(title, body, summary, tags);
}

bool BlogPlugin::updatePost(const QString& id, const QString& title,
                            const QString& body, const QString& summary,
                            const QStringList& tags)
{
    return m_posts->update(id, title, body, summary, tags);
}

bool BlogPlugin::publishPost(const QString& id)
{
    const QString json = m_posts->publish(id);
    return !json.isEmpty();
}

bool BlogPlugin::deletePost(const QString& id)
{
    const QString storageCid = m_posts->getPostCid(id);
    const QString tombstone  = m_posts->remove(id);
    if (tombstone.isEmpty()) return false;

    if (m_chatSync && !m_ownPubkey.isEmpty()) {
        QJsonObject del;
        del["post_id"] = id;
        if (!storageCid.isEmpty()) del["cid"] = storageCid;
        QJsonObject payload;
        payload["delete"] = del;
        const QString envelope = buildSignedEnvelope("delete", payload);
        if (!envelope.isEmpty()) m_chatSync->publishMessage(envelope);
    }
    return true;
}

QString BlogPlugin::getPost(const QString& id)
{
    const QJsonObject post = m_posts->getPost(id);
    if (post.isEmpty()) return {};
    return QString::fromUtf8(QJsonDocument(post).toJson(QJsonDocument::Compact));
}

QString BlogPlugin::listPosts()
{
    return QString::fromUtf8(
        QJsonDocument(m_posts->listPosts()).toJson(QJsonDocument::Compact));
}

QString BlogPlugin::listDrafts()
{
    return QString::fromUtf8(
        QJsonDocument(m_posts->listDrafts()).toJson(QJsonDocument::Compact));
}

// ── Feed / subscriptions ──────────────────────────────────────────────────────

bool BlogPlugin::subscribe(const QString& pubkey, const QString& displayName)
{
    if (!m_feed->subscribe(pubkey, displayName)) return false;

    // Subscribe to the author's Chat blog channel (fetches history + watches live)
    if (m_chatSync) m_chatSync->subscribeToAuthor(pubkey);

    // Also query the LEZ registry for any on-chain CIDs for this author that may
    // predate Chat history, and fetch them from Storage
    if (m_lezRegistry && m_storage) {
        const QVariant regResult = m_lezRegistry->invokeRemoteMethod(
            "lez_registry_module", "get_posts", pubkey);
        const QString cidsJson = regResult.toString();
        if (!cidsJson.isEmpty()) {
            const QJsonArray cids = QJsonDocument::fromJson(cidsJson.toUtf8()).array();
            for (const auto& cidVal : cids) {
                const QString cid = cidVal.toString();
                if (cid.isEmpty()) continue;
                const QString content = fetchFromStorage(cid);
                if (content.isEmpty()) continue;
                const QJsonObject postObj =
                    QJsonDocument::fromJson(content.toUtf8()).object();
                if (postObj.isEmpty()) continue;
                // Build a minimal envelope for FeedStore ingestion
                QJsonObject envelope;
                envelope["version"]   = 1;
                envelope["type"]      = "post";
                envelope["cid"]       = cid;
                QJsonObject author;
                author["pubkey"]      = pubkey;
                author["name"]        = displayName;
                envelope["author"]    = author;
                envelope["timestamp"] = postObj.value("published_at").toString();
                envelope["post"]      = postObj;
                m_feed->ingestPost(envelope);
            }
        }
    }

    emit subscriptionAdded(pubkey);
    return true;
}

bool BlogPlugin::unsubscribe(const QString& pubkey)
{
    if (m_chatSync) m_chatSync->unsubscribeFromAuthor(pubkey);
    return m_feed->unsubscribe(pubkey);
}

QString BlogPlugin::listSubscriptions()
{
    return QString::fromUtf8(
        QJsonDocument(m_feed->listSubscriptions()).toJson(QJsonDocument::Compact));
}

QString BlogPlugin::getFeedPosts(const QString& pubkey)
{
    const QJsonArray posts = pubkey.isEmpty()
        ? m_feed->getAggregatedFeed()
        : m_feed->getPostsByAuthor(pubkey);
    return QString::fromUtf8(QJsonDocument(posts).toJson(QJsonDocument::Compact));
}

QString BlogPlugin::getAggregatedFeed()
{
    return QString::fromUtf8(
        QJsonDocument(m_feed->getAggregatedFeed()).toJson(QJsonDocument::Compact));
}

// ── RSS ───────────────────────────────────────────────────────────────────────

int BlogPlugin::getRssPort()
{
    return m_rss ? m_rss->port() : 8484;
}

bool BlogPlugin::setRssPort(int port)
{
    if (!m_rss || !m_kv) return false;
    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("settings:rss_port"), QString::number(port));
    m_rss->stop();
    return m_rss->start(m_rss->bindAddress(), port);
}

QString BlogPlugin::getRssBindAddress()
{
    return m_rss ? m_rss->bindAddress() : "127.0.0.1";
}

bool BlogPlugin::setRssBindAddress(const QString& address)
{
    if (!m_rss || !m_kv) return false;
    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("settings:rss_bind"), address);
    m_rss->stop();
    return m_rss->start(address, m_rss->port());
}

// ── Search and filtering ───────────────────────────────────────────────────────

static bool postMatchesQuery(const QJsonObject& post, const QString& lowerQuery)
{
    if (post["title"].toString().toLower().contains(lowerQuery)) return true;
    if (post["body"].toString().toLower().contains(lowerQuery)) return true;
    if (post["summary"].toString().toLower().contains(lowerQuery)) return true;
    for (const auto& tag : post["tags"].toArray()) {
        if (tag.toString().toLower().contains(lowerQuery)) return true;
    }
    return false;
}

QString BlogPlugin::searchPosts(const QString& query)
{
    if (query.isEmpty()) return "[]";
    const QString q = query.toLower();

    QJsonArray results;
    for (const auto& p : m_posts->listPosts()) {
        const QJsonObject post = p.toObject();
        if (postMatchesQuery(post, q)) results.append(post);
    }
    for (const auto& p : m_feed->getAggregatedFeed()) {
        const QJsonObject post = p.toObject();
        if (postMatchesQuery(post, q)) results.append(post);
    }
    return QString::fromUtf8(QJsonDocument(results).toJson(QJsonDocument::Compact));
}

QString BlogPlugin::getPostsByTag(const QString& tag)
{
    return QString::fromUtf8(
        QJsonDocument(m_feed->getPostsByTag(tag)).toJson(QJsonDocument::Compact));
}

// ── OPML ──────────────────────────────────────────────────────────────────────

QString BlogPlugin::getOpmlContent()
{
    const QJsonArray subs = m_feed->listSubscriptions();
    const int port = m_rss ? m_rss->port() : 8484;
    const QString bind = m_rss ? m_rss->bindAddress() : "127.0.0.1";
    const QString base = QStringLiteral("http://") + bind + ":" + QString::number(port);

    QString xml;
    QTextStream ts(&xml);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<opml version=\"2.0\">\n"
       << "  <head>\n"
       << "    <title>Logos Blog Subscriptions</title>\n"
       << "    <dateCreated>"
       << QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date)
       << "</dateCreated>\n"
       << "  </head>\n"
       << "  <body>\n";
    for (const auto& s : subs) {
        const QJsonObject sub = s.toObject();
        const QString pubkey = sub["pubkey"].toString();
        const QString name   = sub["name"].toString().toHtmlEscaped();
        ts << "    <outline type=\"rss\" text=\"" << name
           << "\" xmlUrl=\"" << base << "/@" << pubkey << "/feed.xml"
           << "\" htmlUrl=\"" << base << "/@" << pubkey
           << "\" pubkey=\"" << pubkey << "\"/>\n";
    }
    ts << "  </body>\n</opml>\n";
    ts.flush();
    return xml;
}

bool BlogPlugin::importOpml(const QString& xml)
{
    static const QRegularExpression pubkeyFromUrl(R"(/@([0-9a-f]{64})/feed\.xml)");

    QXmlStreamReader reader(xml.toUtf8());
    int imported = 0;
    while (!reader.atEnd() && !reader.hasError()) {
        reader.readNext();
        if (reader.tokenType() != QXmlStreamReader::StartElement) continue;
        if (reader.name() != QStringView(u"outline")) continue;

        const QXmlStreamAttributes attrs = reader.attributes();
        const QString xmlUrl = attrs.value("xmlUrl").toString();
        const QString name   = attrs.value("text").toString();

        QString pubkey = attrs.value("pubkey").toString();
        if (pubkey.isEmpty() && !xmlUrl.isEmpty()) {
            const QRegularExpressionMatch m = pubkeyFromUrl.match(xmlUrl);
            if (m.hasMatch()) pubkey = m.captured(1);
        }
        if (pubkey.isEmpty()) continue;

        subscribe(pubkey, name.isEmpty() ? pubkey.left(8) : name);
        ++imported;
    }
    return imported > 0;
}

// ── LEZ registry ──────────────────────────────────────────────────────────────

QString BlogPlugin::getRegistryCids(const QString& authorPubkey)
{
    if (!m_lezRegistry || authorPubkey.isEmpty()) return "[]";
    const QVariant result = m_lezRegistry->invokeRemoteMethod(
        "lez_registry_module", "get_posts", authorPubkey);
    const QString json = result.toString();
    return json.isEmpty() ? QStringLiteral("[]") : json;
}
