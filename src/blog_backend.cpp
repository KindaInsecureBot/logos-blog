#include "blog_backend.h"
#include "module_proxy.h"

#include <QJsonDocument>
#include <QJsonObject>

BlogBackend::BlogBackend(QObject* parent)
    : QObject(parent)
{}

void BlogBackend::initLogos(LogosAPI* api)
{
    m_api = api;
    m_blogModule = api->getClient("blog_module");
    if (!m_blogModule) {
        emit errorOccurred("blog_module not available");
        return;
    }
    connectSignals();
    refreshIdentity();
    refreshRssState();
}

void BlogBackend::connectSignals()
{
    m_api->on("blog_module", "postPublished", [this](QVariantList args) {
        emit postPublished(args.value(0).toString());
    });
    m_api->on("blog_module", "postReceived", [this](QVariantList args) {
        emit postReceived(args.value(0).toString());
    });
    m_api->on("blog_module", "postDeleted", [this](QVariantList args) {
        emit postDeleted(args.value(0).toString(), args.value(1).toString());
    });
    m_api->on("blog_module", "profileUpdated", [this](QVariantList args) {
        emit profileUpdated(args.value(0).toString(), args.value(1).toString());
    });
    m_api->on("blog_module", "identityChanged", [this](QVariantList) {
        refreshIdentity();
    });
    m_api->on("blog_module", "wakuStarted", [this](QVariantList) {
        m_wakuConnected = true;
        emit wakuStateChanged();
    });

    // Waku is considered connected if delivery_module is present
    if (m_api->getClient("delivery_module")) {
        m_wakuConnected = true;
        emit wakuStateChanged();
    }
}

void BlogBackend::refreshIdentity()
{
    if (!m_blogModule) return;
    QVariant result = m_blogModule->invokeRemoteMethod("blog_module", "getIdentity");
    const QString json = result.toString();
    if (json.isEmpty()) return;

    const QJsonObject identity = QJsonDocument::fromJson(json.toUtf8()).object();
    m_ownPubkey    = identity["pubkey"].toString();
    m_displayName  = identity["display_name"].toString();
    m_bio          = identity["bio"].toString();
    emit identityChanged();
}

void BlogBackend::refreshRssState()
{
    if (!m_blogModule) return;
    QVariant portResult = m_blogModule->invokeRemoteMethod("blog_module", "getRssPort");
    QVariant bindResult = m_blogModule->invokeRemoteMethod("blog_module", "getRssBindAddress");

    m_rssPort        = portResult.toInt();
    m_rssBindAddress = bindResult.toString();
    m_rssRunning     = m_rssPort > 0;
    emit rssStateChanged();
}

// ── Property getters ──────────────────────────────────────────────────────────

QString BlogBackend::ownPubkey()      const { return m_ownPubkey; }
QString BlogBackend::displayName()    const { return m_displayName; }
QString BlogBackend::bio()            const { return m_bio; }
bool    BlogBackend::rssRunning()     const { return m_rssRunning; }
int     BlogBackend::rssPort()        const { return m_rssPort; }
QString BlogBackend::rssBindAddress() const { return m_rssBindAddress; }
bool    BlogBackend::wakuConnected()  const { return m_wakuConnected; }

// ── Invokable methods ─────────────────────────────────────────────────────────

void BlogBackend::setIdentity(const QString& displayName, const QString& bio)
{
    if (!m_blogModule) return;
    m_blogModule->invokeRemoteMethod("blog_module", "setIdentity", displayName, bio);
}

QString BlogBackend::createPost(const QString& title, const QString& body,
                                const QString& summary, const QStringList& tags)
{
    if (!m_blogModule) return {};
    return m_blogModule->invokeRemoteMethod("blog_module", "createPost",
        title, body, summary, QVariant(tags)).toString();
}

bool BlogBackend::updatePost(const QString& id, const QString& title,
                             const QString& body, const QString& summary,
                             const QStringList& tags)
{
    if (!m_blogModule) return false;
    return m_blogModule->invokeRemoteMethod("blog_module", "updatePost",
        id, title, body, summary, QVariant(tags)).toBool();
}

bool BlogBackend::publishPost(const QString& id)
{
    if (!m_blogModule) return false;
    return m_blogModule->invokeRemoteMethod("blog_module", "publishPost", id).toBool();
}

bool BlogBackend::deletePost(const QString& id)
{
    if (!m_blogModule) return false;
    return m_blogModule->invokeRemoteMethod("blog_module", "deletePost", id).toBool();
}

QString BlogBackend::getPost(const QString& id)
{
    if (!m_blogModule) return "{}";
    return m_blogModule->invokeRemoteMethod("blog_module", "getPost", id).toString();
}

QString BlogBackend::listPosts()
{
    if (!m_blogModule) return "[]";
    return m_blogModule->invokeRemoteMethod("blog_module", "listPosts").toString();
}

QString BlogBackend::listDrafts()
{
    if (!m_blogModule) return "[]";
    return m_blogModule->invokeRemoteMethod("blog_module", "listDrafts").toString();
}

bool BlogBackend::subscribe(const QString& pubkey, const QString& name)
{
    if (!m_blogModule) return false;
    return m_blogModule->invokeRemoteMethod("blog_module", "subscribe",
        pubkey, name).toBool();
}

bool BlogBackend::unsubscribe(const QString& pubkey)
{
    if (!m_blogModule) return false;
    return m_blogModule->invokeRemoteMethod("blog_module", "unsubscribe", pubkey).toBool();
}

QString BlogBackend::listSubscriptions()
{
    if (!m_blogModule) return "[]";
    return m_blogModule->invokeRemoteMethod("blog_module", "listSubscriptions").toString();
}

QString BlogBackend::getAggregatedFeed()
{
    if (!m_blogModule) return "[]";
    return m_blogModule->invokeRemoteMethod("blog_module", "getAggregatedFeed").toString();
}

QString BlogBackend::getFeedByAuthor(const QString& pubkey)
{
    if (!m_blogModule) return "[]";
    return m_blogModule->invokeRemoteMethod("blog_module", "getFeedPosts", pubkey).toString();
}

bool BlogBackend::setRssPort(int port)
{
    if (!m_blogModule) return false;
    bool ok = m_blogModule->invokeRemoteMethod("blog_module", "setRssPort",
        QVariant(port)).toBool();
    if (ok) refreshRssState();
    return ok;
}

bool BlogBackend::setRssBindAddress(const QString& address)
{
    if (!m_blogModule) return false;
    bool ok = m_blogModule->invokeRemoteMethod("blog_module", "setRssBindAddress",
        address).toBool();
    if (ok) refreshRssState();
    return ok;
}
