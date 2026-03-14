#include "blog_plugin.h"
#include "module_proxy.h"

#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

BlogPlugin::BlogPlugin(QObject* parent)
    : QObject(parent)
    , m_posts(new PostStore(this))
    , m_feed(new FeedStore(this))
    , m_waku(new WakuSync(this))
    , m_rss(new RssServer(this))
{}

void BlogPlugin::initLogos(LogosAPI* api)
{
    // REQUIRED: set base class field first so ModuleProxy calls work
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

    // ── delivery_module ───────────────────────────────────────────────────
    m_delivery = api->getClient("delivery_module");
    if (m_delivery) {
        m_waku->setDeliveryClient(m_delivery);
    }

    // Connect PostStore signal → build signed Waku envelope → publish
    connect(m_posts, &PostStore::postPublished,
            this, [this](const QString& /*id*/, const QString& postJson) {
        const QJsonObject post = QJsonDocument::fromJson(postJson.toUtf8()).object();
        QJsonObject payload;
        payload["post"] = post;
        const QString signedEnvelope = buildSignedEnvelope("post", payload);
        const QString toEmit = signedEnvelope.isEmpty() ? postJson : signedEnvelope;
        emit postPublished(toEmit);
        if (m_waku) m_waku->publishPost(toEmit);
    });

    // Connect FeedStore signals to plugin signals
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

    // Route WakuSync::messageReceived → FeedStore ingestion
    connect(m_waku, &WakuSync::messageReceived,
            this, [this](const QString& /*topic*/, const QString& payloadJson) {
        const QJsonObject envelope = QJsonDocument::fromJson(payloadJson.toUtf8()).object();
        if (envelope.isEmpty()) return;
        const QString type = envelope["type"].toString();
        if (type == "post") {
            m_feed->ingestPost(envelope);
        } else if (type == "delete") {
            m_feed->ingestDelete(envelope);
        } else if (type == "profile") {
            m_feed->ingestProfile(envelope);
        }
    });

    connect(m_waku, &WakuSync::nodeStarted, this, [this]() {
        emit wakuStarted();
    });

    // Load or create identity (sets m_ownPubkey / m_ownPrivkey)
    loadOrCreateIdentity();

    // Connect delivery_module events → WakuSync (must happen after identity is loaded)
    connectDeliveryModule();

    // Start RSS server
    startRssServer();

    // Start Waku node, then re-subscribe to all saved author subscriptions
    m_waku->start();
    for (const QString& pk : m_feed->subscribedPubkeys()) {
        m_waku->subscribeToAuthor(pk);
        m_waku->requestHistory(pk, QDateTime::currentDateTimeUtc().addDays(-30));
    }
}

void BlogPlugin::connectDeliveryModule()
{
    if (!m_delivery) return;

    m_api->on("delivery_module", "messageReceived", [this](QVariantList args) {
        if (args.size() < 3) return;
        const QString topic      = args.value(1).toString();
        const QString b64payload = args.value(2).toString();

        // Subscription gate: only process from subscribed authors + own topic
        const QStringList parts = topic.split('/');
        const QString topicPubkey = parts.size() >= 4 ? parts.at(3) : QString();
        if (topicPubkey.isEmpty()) return;
        if (topicPubkey != m_ownPubkey && !m_feed->isSubscribed(topicPubkey)) return;

        m_waku->onDeliveryMessage(topic, b64payload);
    });
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
            if (m_waku) m_waku->setOwnPubkey(pubkey);
            return;
        }
    }

    // No valid identity — generate Ed25519 keypair
    const Keypair kp = Crypto::generateEd25519Keypair();
    if (kp.pubkeyHex.isEmpty()) return;  // OpenSSL failure

    QJsonObject identity;
    identity["pubkey"]       = kp.pubkeyHex;
    identity["privkey"]      = kp.privkeyHex;  // TODO Phase 6: encrypt at rest
    identity["display_name"] = "Logos User";
    identity["bio"]          = QString();
    identity["created_at"]   = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("identity"),
        QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));

    m_ownPubkey   = kp.pubkeyHex;
    m_ownPrivkey  = kp.privkeyHex;
    m_displayName = "Logos User";
    if (m_waku) m_waku->setOwnPubkey(m_ownPubkey);
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

QString BlogPlugin::buildSignedEnvelope(const QString& type, const QJsonObject& typePayload)
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

    // Merge type-specific payload fields
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

    // Return identity without the private key
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

    // Publish updated profile over Waku
    if (m_waku && !m_ownPubkey.isEmpty()) {
        QJsonObject profile;
        profile["name"]       = displayName;
        profile["bio"]        = bio;
        profile["avatar_url"] = QString();
        QJsonObject payload;
        payload["profile"] = profile;
        const QString envelope = buildSignedEnvelope("profile", payload);
        if (!envelope.isEmpty()) m_waku->publishPost(envelope);  // reuse publishPost for raw envelope
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
    if (m_waku) m_waku->setOwnPubkey(m_ownPubkey);
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
    const QString tombstone = m_posts->remove(id);
    if (tombstone.isEmpty()) return false;

    if (m_waku && !m_ownPubkey.isEmpty() && m_delivery) {
        QJsonObject del;
        del["post_id"] = id;
        QJsonObject payload;
        payload["delete"] = del;
        const QString envelope = buildSignedEnvelope("delete", payload);
        if (!envelope.isEmpty()) {
            const QString topic = QStringLiteral("/logos-blog/1/") + m_ownPubkey + "/json";
            const QString b64 = QString::fromLatin1(envelope.toUtf8().toBase64());
            m_delivery->invokeRemoteMethod("delivery_module", "send", topic, b64);
        }
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
    if (m_waku) {
        m_waku->subscribeToAuthor(pubkey);
        m_waku->requestHistory(pubkey,
            QDateTime::currentDateTimeUtc().addDays(-30));
    }
    emit subscriptionAdded(pubkey);
    return true;
}

bool BlogPlugin::unsubscribe(const QString& pubkey)
{
    if (m_waku) m_waku->unsubscribeFromAuthor(pubkey);
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
