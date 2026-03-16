#include "waku_sync.h"
#include "logos_api_client.h"

#include <QDateTime>

WakuSync::WakuSync(QObject* parent)
    : QObject(parent)
{}

void WakuSync::setDeliveryClient(LogosAPIClient* delivery)
{
    m_delivery = delivery;
}

void WakuSync::setOwnPubkey(const QString& pubkeyHex)
{
    m_ownPubkey = pubkeyHex;
}

// Phase 3+ — stub. Does nothing without delivery_module.
void WakuSync::start()
{
    if (!m_delivery) {
        emit nodeStarted(); // report as started even without delivery (Phase 1/2)
        return;
    }

    m_delivery->invokeRemoteMethod("delivery_module", "createNode",
        QString(R"({"logLevel":"WARN","mode":"Core","preset":"logos.dev"})"));
    m_delivery->invokeRemoteMethod("delivery_module", "start");

    if (!m_ownPubkey.isEmpty()) {
        subscribeToAuthor(m_ownPubkey);
    }

    emit nodeStarted();
}

void WakuSync::publishPost(const QString& signedEnvelopeJson)
{
    if (!m_delivery || m_ownPubkey.isEmpty()) return;
    const QString topic = topicForPubkey(m_ownPubkey);
    const QString payload = signedEnvelopeJson.toUtf8().toBase64();
    m_delivery->invokeRemoteMethod("delivery_module", "send", topic, payload);
}

void WakuSync::publishDelete(const QString& postId)
{
    if (!m_delivery || m_ownPubkey.isEmpty()) return;
    const QString topic = topicForPubkey(m_ownPubkey);
    const QString envelope = QStringLiteral(R"({"version":1,"type":"delete","post_id":")") + postId + "\"}";
    const QString payload = envelope.toUtf8().toBase64();
    m_delivery->invokeRemoteMethod("delivery_module", "send", topic, payload);
}

void WakuSync::publishProfile(const QString& displayName, const QString& bio)
{
    if (!m_delivery || m_ownPubkey.isEmpty()) return;
    const QString topic = topicForPubkey(m_ownPubkey);
    const QString envelope = QStringLiteral(R"({"version":1,"type":"profile","name":")") +
        displayName + R"(","bio":")" + bio + "\"}";
    const QString payload = envelope.toUtf8().toBase64();
    m_delivery->invokeRemoteMethod("delivery_module", "send", topic, payload);
}

void WakuSync::subscribeToAuthor(const QString& pubkeyHex)
{
    if (!m_delivery || pubkeyHex.isEmpty()) return;
    const QString topic = topicForPubkey(pubkeyHex);
    if (m_subscribedTopics.contains(topic)) return;
    m_subscribedTopics.insert(topic);
    m_delivery->invokeRemoteMethod("delivery_module", "subscribe", topic);
}

void WakuSync::unsubscribeFromAuthor(const QString& pubkeyHex)
{
    if (!m_delivery || pubkeyHex.isEmpty()) return;
    const QString topic = topicForPubkey(pubkeyHex);
    m_subscribedTopics.remove(topic);
    m_delivery->invokeRemoteMethod("delivery_module", "unsubscribe", topic);
}

void WakuSync::requestHistory(const QString& pubkeyHex, const QDateTime& since)
{
    if (!m_delivery || pubkeyHex.isEmpty()) return;
    const QString topic = topicForPubkey(pubkeyHex);
    const QString sinceIso = since.toUTC().toString(Qt::ISODate);
    m_delivery->invokeRemoteMethod("delivery_module", "queryHistory", topic, sinceIso);
}

void WakuSync::onDeliveryMessage(const QString& topic, const QString& base64payload)
{
    const QByteArray raw = QByteArray::fromBase64(base64payload.toLatin1());
    emit messageReceived(topic, QString::fromUtf8(raw));
}

QString WakuSync::topicForPubkey(const QString& pubkeyHex)
{
    return QStringLiteral("/logos-blog/1/") + pubkeyHex + "/json";
}
