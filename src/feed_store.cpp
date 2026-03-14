#include "feed_store.h"
#include "module_proxy.h"

#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>

FeedStore::FeedStore(QObject* parent)
    : QObject(parent)
{}

void FeedStore::setKvClient(ModuleProxy* kv)
{
    m_kv = kv;
}

bool FeedStore::subscribe(const QString& pubkey, const QString& displayName)
{
    if (!m_kv || pubkey.isEmpty()) return false;

    QJsonObject sub;
    sub["pubkey"]        = pubkey;
    sub["name"]          = displayName;
    sub["topic"]         = QStringLiteral("/logos-blog/1/") + pubkey + "/json";
    sub["subscribed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    sub["last_seen"]     = QString();

    const QString key = QStringLiteral("subscriptions:") + pubkey;
    const QString val = QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact));
    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key, val);
    return true;
}

bool FeedStore::unsubscribe(const QString& pubkey)
{
    if (!m_kv || pubkey.isEmpty()) return false;
    const QString key = QStringLiteral("subscriptions:") + pubkey;
    m_kv->invokeRemoteMethod("kv_module", "remove", QString(NS), key);

    // Also remove cached feed posts
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (!json.isEmpty()) {
        const QJsonArray items = QJsonDocument::fromJson(json.toUtf8()).array();
        const QString prefix = QStringLiteral("feed:") + pubkey + ":";
        for (const auto& item : items) {
            const QString k = item.toObject()["key"].toString();
            if (k.startsWith(prefix)) {
                m_kv->invokeRemoteMethod("kv_module", "remove", QString(NS), k);
            }
        }
    }
    return true;
}

QJsonArray FeedStore::listSubscriptions()
{
    if (!m_kv) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    QJsonArray subs;
    for (const auto& item : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject entry = item.toObject();
        if (entry["key"].toString().startsWith("subscriptions:")) {
            const QJsonObject sub = QJsonDocument::fromJson(
                entry["value"].toString().toUtf8()).object();
            if (!sub.isEmpty()) subs.append(sub);
        }
    }
    return subs;
}

bool FeedStore::isSubscribed(const QString& pubkey) const
{
    if (!m_kv || pubkey.isEmpty()) return false;
    const QString key = QStringLiteral("subscriptions:") + pubkey;
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
    return !result.toString().isEmpty();
}

void FeedStore::updateLastSeen(const QString& pubkey)
{
    if (!m_kv || pubkey.isEmpty()) return;
    const QString key = QStringLiteral("subscriptions:") + pubkey;
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
    const QString json = result.toString();
    if (json.isEmpty()) return;

    QJsonObject sub = QJsonDocument::fromJson(json.toUtf8()).object();
    sub["last_seen"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key,
        QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact)));
}

bool FeedStore::ingestPost(const QJsonObject& envelope)
{
    if (!m_kv) return false;

    const QJsonObject post = envelope["post"].toObject();
    const QJsonObject author = envelope["author"].toObject();
    const QString pubkey = author["pubkey"].toString();
    const QString postId = post["id"].toString();

    if (pubkey.isEmpty() || postId.isEmpty()) return false;
    if (!isSubscribed(pubkey)) return false;

    QJsonObject stored = post;
    stored["author_pubkey"] = pubkey;
    stored["author_name"]   = author["name"].toString();
    stored["signature"]     = envelope["signature"].toString();
    stored["verified"]      = false; // real verification in Phase 3
    stored["received_at"]   = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QString key = QStringLiteral("feed:") + pubkey + ":" + postId;
    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key,
        QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)));

    updateLastSeen(pubkey);
    emit postIngested(pubkey, postId);
    return true;
}

bool FeedStore::ingestDelete(const QString& authorPubkey, const QString& postId)
{
    if (!m_kv || authorPubkey.isEmpty() || postId.isEmpty()) return false;
    const QString key = QStringLiteral("feed:") + authorPubkey + ":" + postId;
    m_kv->invokeRemoteMethod("kv_module", "remove", QString(NS), key);
    emit postDeleted(authorPubkey, postId);
    return true;
}

bool FeedStore::ingestProfile(const QString& pubkey, const QJsonObject& profile)
{
    if (!m_kv || pubkey.isEmpty()) return false;
    const QString key = QStringLiteral("profiles:") + pubkey;
    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key,
        QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact)));

    // Update name in subscription record
    const QString subKey = QStringLiteral("subscriptions:") + pubkey;
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), subKey);
    const QString subJson = result.toString();
    if (!subJson.isEmpty()) {
        QJsonObject sub = QJsonDocument::fromJson(subJson.toUtf8()).object();
        sub["name"] = profile["name"].toString();
        m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), subKey,
            QString::fromUtf8(QJsonDocument(sub).toJson(QJsonDocument::Compact)));
    }

    emit profileUpdated(pubkey, profile["name"].toString());
    return true;
}

QJsonArray FeedStore::getPostsByAuthor(const QString& pubkey)
{
    if (!m_kv || pubkey.isEmpty()) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    const QString prefix = QStringLiteral("feed:") + pubkey + ":";
    QJsonArray posts;
    for (const auto& item : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject entry = item.toObject();
        if (entry["key"].toString().startsWith(prefix)) {
            const QJsonObject post = QJsonDocument::fromJson(
                entry["value"].toString().toUtf8()).object();
            if (!post.isEmpty()) posts.append(post);
        }
    }

    std::sort(posts.begin(), posts.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });
    return posts;
}

QJsonArray FeedStore::getAggregatedFeed()
{
    if (!m_kv) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    QJsonArray posts;
    for (const auto& item : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject entry = item.toObject();
        if (entry["key"].toString().startsWith("feed:")) {
            const QJsonObject post = QJsonDocument::fromJson(
                entry["value"].toString().toUtf8()).object();
            if (!post.isEmpty()) posts.append(post);
        }
    }

    std::sort(posts.begin(), posts.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });
    return posts;
}

QJsonObject FeedStore::getPost(const QString& authorPubkey, const QString& postId)
{
    if (!m_kv || authorPubkey.isEmpty() || postId.isEmpty()) return {};
    const QString key = QStringLiteral("feed:") + authorPubkey + ":" + postId;
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
    return QJsonDocument::fromJson(result.toString().toUtf8()).object();
}

QStringList FeedStore::subscribedPubkeys() const
{
    if (!m_kv) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    QStringList keys;
    for (const auto& item : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QString k = item.toObject()["key"].toString();
        if (k.startsWith("subscriptions:")) {
            keys.append(k.mid(QStringLiteral("subscriptions:").length()));
        }
    }
    return keys;
}
