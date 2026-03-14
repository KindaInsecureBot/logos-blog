#pragma once
#include "crypto.h"
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class ModuleProxy;

// Phase 3+ implementation. Stub for Phase 1/2.
class FeedStore : public QObject {
    Q_OBJECT
public:
    explicit FeedStore(QObject* parent = nullptr);

    void setKvClient(ModuleProxy* kv);

    // Subscriptions
    bool       subscribe(const QString& pubkey, const QString& displayName);
    bool       unsubscribe(const QString& pubkey);
    QJsonArray listSubscriptions();
    bool       isSubscribed(const QString& pubkey) const;
    void       updateLastSeen(const QString& pubkey);

    // Ingestion — called by WakuSync on messageReceived.
    // Each method takes the full signed envelope for signature verification.
    bool ingestPost(const QJsonObject& envelope);
    bool ingestDelete(const QJsonObject& envelope);
    bool ingestProfile(const QJsonObject& envelope);

    // Feed queries
    QJsonArray  getPostsByAuthor(const QString& pubkey);
    QJsonArray  getAggregatedFeed();
    QJsonObject getPost(const QString& authorPubkey, const QString& postId);

    // Returns list of all subscribed pubkeys (used by WakuSync::start)
    QStringList subscribedPubkeys() const;

signals:
    void postIngested(const QString& authorPubkey, const QString& postId);
    void postDeleted(const QString& authorPubkey, const QString& postId);
    void profileUpdated(const QString& pubkey, const QString& displayName);

private:
    ModuleProxy* m_kv = nullptr;

    static constexpr const char* NS = "blog";
};
