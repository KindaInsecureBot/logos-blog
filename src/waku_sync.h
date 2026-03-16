#pragma once
#include <QObject>
#include <QSet>

class LogosAPIClient;

// Phase 3+ implementation. Stub for Phase 1/2.
class WakuSync : public QObject {
    Q_OBJECT
public:
    explicit WakuSync(QObject* parent = nullptr);

    void setDeliveryClient(LogosAPIClient* delivery);
    void setOwnPubkey(const QString& pubkeyHex);

    // Called once in BlogPlugin::initLogos after node is started
    void start();

    // Publish own post — called by BlogPlugin::publishPost
    void publishPost(const QString& signedEnvelopeJson);

    // Publish delete tombstone
    void publishDelete(const QString& postId);

    // Publish profile update
    void publishProfile(const QString& displayName, const QString& bio);

    // Subscribe to an author's Waku topic
    void subscribeToAuthor(const QString& pubkeyHex);

    // Unsubscribe from an author's Waku topic
    void unsubscribeFromAuthor(const QString& pubkeyHex);

    // Replay history for a topic (best-effort)
    void requestHistory(const QString& pubkeyHex, const QDateTime& since);

    // Called by BlogPlugin when delivery_module fires messageReceived.
    // Decodes base64 payload and emits messageReceived signal.
    void onDeliveryMessage(const QString& topic, const QString& base64payload);

signals:
    void messageReceived(const QString& topic, const QString& payloadJson);
    void nodeStarted();
    void deliveryError(const QString& error);

private:
    LogosAPIClient*  m_delivery  = nullptr;
    QString       m_ownPubkey;
    QSet<QString> m_subscribedTopics;

    static QString topicForPubkey(const QString& pubkeyHex);
};
