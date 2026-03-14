#include "blog_plugin.h"
#include "module_proxy.h"

#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>

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
        // Switch to FileBackend for persistence across restarts
        m_kv->invokeRemoteMethod("kv_module", "setDataDir",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/blog-data");

        m_posts->setKvClient(m_kv);
        m_feed->setKvClient(m_kv);
    }

    // ── delivery_module (optional — Phase 3+) ─────────────────────────────
    m_delivery = api->getClient("delivery_module");
    if (m_delivery) {
        m_waku->setDeliveryClient(m_delivery);
    }

    // Connect PostStore signals to plugin signals
    connect(m_posts, &PostStore::postPublished,
            this, [this](const QString& /*id*/, const QString& json) {
        emit postPublished(json);
        if (m_waku) m_waku->publishPost(json);
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

    // Connect WakuSync messages to FeedStore
    if (m_waku) {
        connect(m_waku, &WakuSync::messageReceived,
                this, [this](const QString& /*topic*/, const QString& payloadJson) {
            const QJsonObject envelope = QJsonDocument::fromJson(
                payloadJson.toUtf8()).object();
            if (envelope.isEmpty()) return;
            const QString type = envelope["type"].toString();
            if (type == "post") {
                m_feed->ingestPost(envelope);
            } else if (type == "delete") {
                const QString authorPubkey = envelope["author"].toObject()["pubkey"].toString();
                m_feed->ingestDelete(authorPubkey, envelope["post_id"].toString());
            } else if (type == "profile") {
                const QString pubkey = envelope["author"].toObject()["pubkey"].toString();
                m_feed->ingestProfile(pubkey, envelope);
            }
        });
    }

    // Load or create identity
    loadOrCreateIdentity();

    // Start RSS server
    startRssServer();

    // Start Waku node
    if (m_waku) {
        m_waku->start();
    }
}

void BlogPlugin::loadOrCreateIdentity()
{
    if (!m_kv) return;

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                                               QString("blog"), QString("identity"));
    if (!result.toString().isEmpty()) {
        // Identity already exists
        const QJsonObject identity = QJsonDocument::fromJson(
            result.toString().toUtf8()).object();
        const QString pubkey = identity["pubkey"].toString();
        if (m_waku) m_waku->setOwnPubkey(pubkey);
        return;
    }

    // No identity yet — create a placeholder (real keygen in Phase 3)
    QJsonObject identity;
    identity["pubkey"]            = QString();
    identity["privkey_encrypted"] = QString();
    identity["display_name"]      = "Logos User";
    identity["bio"]               = QString();
    identity["created_at"]        = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    m_kv->invokeRemoteMethod("kv_module", "set",
        QString("blog"), QString("identity"),
        QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));
}

void BlogPlugin::startRssServer()
{
    m_rss->setPostStore(m_posts);
    m_rss->setFeedStore(m_feed);

    // Load port/bind settings from kv
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

// ── Identity ──────────────────────────────────────────────────────────────────

QString BlogPlugin::getIdentity()
{
    if (!m_kv) return "{}";
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get",
                          QString("blog"), QString("identity"));
    return result.toString().isEmpty() ? "{}" : result.toString();
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

    emit identityChanged();
    return true;
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
    if (m_waku) m_waku->publishDelete(id);
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
