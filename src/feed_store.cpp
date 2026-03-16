#include "feed_store.h"
#include "logos_api_client.h"
#include "crypto.h"

#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>
#include <QHash>

FeedStore::FeedStore(QObject* parent)
    : QObject(parent)
{}

void FeedStore::setKvClient(LogosAPIClient* kv)
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

// Verify the Ed25519 signature on an envelope.
// The signature covers compact JSON of the envelope with "signature" key removed.
static bool verifyEnvelopeSignature(const QJsonObject& envelope)
{
    const QString sigHex = envelope["signature"].toString();
    if (sigHex.isEmpty()) return false;

    const QString pubkey = envelope["author"].toObject()["pubkey"].toString();
    if (pubkey.isEmpty()) return false;

    QJsonObject toVerify = envelope;
    toVerify.remove("signature");
    const QByteArray canonical = QJsonDocument(toVerify).toJson(QJsonDocument::Compact);
    return Crypto::verify(pubkey, sigHex, canonical);
}

bool FeedStore::ingestPost(const QJsonObject& envelope)
{
    if (!m_kv) return false;

    const QJsonObject authorObj = envelope["author"].toObject();
    const QString pubkey = authorObj["pubkey"].toString();
    const QJsonObject post = envelope["post"].toObject();
    const QString postId = post["id"].toString();

    if (pubkey.isEmpty() || postId.isEmpty()) return false;
    if (!isSubscribed(pubkey)) return false;

    // Verify signature — silently drop invalid messages
    if (!verifyEnvelopeSignature(envelope)) return false;

    // Rate limiting: max kMaxMsgsPerWindow messages per kRateWindowSecs seconds per author
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    RateWindow& rw = m_rateLimiter[pubkey];
    if (nowSecs - rw.windowStartSecs >= kRateWindowSecs) {
        rw.windowStartSecs = nowSecs;
        rw.count = 0;
    }
    if (rw.count >= kMaxMsgsPerWindow) return false;
    ++rw.count;

    // Drop oversized post bodies
    if (post["body"].toString().toUtf8().size() > kMaxPostBodyBytes) return false;

    const QString key = QStringLiteral("feed:") + pubkey + ":" + postId;

    // Cap: if this is a new post (key not present), check per-author limit
    {
        QVariant existCheck = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
        if (existCheck.toString().isEmpty()) {
            // New post — check author post count
            const int count = getPostsByAuthor(pubkey).size();
            if (count >= kMaxPostsPerAuthor) return false;
        }
    }

    // Last-write-wins conflict resolution using updated_at
    QVariant existing = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
    if (!existing.toString().isEmpty()) {
        const QJsonObject stored = QJsonDocument::fromJson(existing.toString().toUtf8()).object();
        const QDateTime incomingTs = QDateTime::fromString(
            post["updated_at"].toString(), Qt::ISODate);
        const QDateTime storedTs = QDateTime::fromString(
            stored["updated_at"].toString(), Qt::ISODate);

        if (incomingTs < storedTs) return false;  // older version — discard
        if (incomingTs == storedTs) {
            // Tie-break: lexicographically larger signature wins (deterministic)
            if (envelope["signature"].toString() <= stored["signature"].toString()) return false;
        }
    }

    QJsonObject toStore = post;
    toStore["author_pubkey"] = pubkey;
    toStore["author_name"]   = authorObj["name"].toString();
    toStore["signature"]     = envelope["signature"].toString();
    toStore["verified"]      = true;
    toStore["received_at"]   = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key,
        QString::fromUtf8(QJsonDocument(toStore).toJson(QJsonDocument::Compact)));

    updateLastSeen(pubkey);
    emit postIngested(pubkey, postId);
    return true;
}

bool FeedStore::ingestDelete(const QJsonObject& envelope)
{
    if (!m_kv) return false;

    const QString pubkey = envelope["author"].toObject()["pubkey"].toString();
    const QString postId = envelope["delete"].toObject()["post_id"].toString();
    if (pubkey.isEmpty() || postId.isEmpty()) return false;

    // Verify signature before deleting — prevents spoofed tombstones
    if (!verifyEnvelopeSignature(envelope)) return false;

    const QString key = QStringLiteral("feed:") + pubkey + ":" + postId;
    m_kv->invokeRemoteMethod("kv_module", "remove", QString(NS), key);
    emit postDeleted(pubkey, postId);
    return true;
}

bool FeedStore::ingestProfile(const QJsonObject& envelope)
{
    if (!m_kv) return false;

    const QString pubkey = envelope["author"].toObject()["pubkey"].toString();
    if (pubkey.isEmpty()) return false;

    // Verify signature
    if (!verifyEnvelopeSignature(envelope)) return false;

    const QJsonObject profile = envelope["profile"].toObject();
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

    QList<QJsonValue> sorted(posts.begin(), posts.end());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });
    posts = QJsonArray();
    for (const auto& v : sorted) posts.append(v);
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

    QList<QJsonValue> sorted(posts.begin(), posts.end());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });
    posts = QJsonArray();
    for (const auto& v : sorted) posts.append(v);
    return posts;
}

QJsonArray FeedStore::getPostsByTag(const QString& tag)
{
    if (tag.isEmpty()) return getAggregatedFeed();

    const QJsonArray all = getAggregatedFeed();
    QJsonArray result;
    for (const auto& p : all) {
        const QJsonObject post = p.toObject();
        for (const auto& t : post["tags"].toArray()) {
            if (t.toString() == tag) {
                result.append(post);
                break;
            }
        }
    }
    return result;
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
