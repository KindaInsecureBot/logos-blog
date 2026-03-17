#include "chat_sync.h"
#include "module_proxy.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ChatSync::ChatSync(QObject* parent)
    : QObject(parent)
{}

void ChatSync::setChatClient(ModuleProxy* chat)
{
    m_chat = chat;
}

void ChatSync::setOwnPubkey(const QString& pubkeyHex)
{
    m_ownPubkey  = pubkeyHex;
    m_ownConvoId = blogConvoIdForPubkey(pubkeyHex);
}

// static
QString ChatSync::blogConvoIdForPubkey(const QString& pubkeyHex)
{
    const QByteArray input =
        QStringLiteral("logos-blog-channel:").toUtf8() + pubkeyHex.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

void ChatSync::start()
{
    if (!m_chat) {
        emit chatStarted(); // no chat module available; proceed in offline mode
        return;
    }

    // Initialise the Chat SDK node with a minimal config
    const QString config = QStringLiteral(
        R"({"logLevel":"WARN","preset":"logos.dev"})");
    m_chat->invokeRemoteMethod("chat_module", "initChat", config);
    m_chat->invokeRemoteMethod("chat_module", "startChat");

    // Watch own blog channel so self-published notifications round-trip correctly
    if (!m_ownConvoId.isEmpty()) {
        m_watchedConvos.insert(m_ownConvoId);
        m_chat->invokeRemoteMethod("chat_module", "getConversation", m_ownConvoId);
    }

    emit chatStarted();
}

void ChatSync::publishMessage(const QString& signedEnvelopeJson)
{
    if (!m_chat || m_ownConvoId.isEmpty()) return;
    // Encode envelope as hex for wire transport (Chat SDK uses hex content)
    const QString contentHex = QString::fromLatin1(
        signedEnvelopeJson.toUtf8().toHex());
    m_chat->invokeRemoteMethod("chat_module", "sendMessage",
                               m_ownConvoId, contentHex);
}

void ChatSync::subscribeToAuthor(const QString& pubkeyHex)
{
    if (pubkeyHex.isEmpty()) return;
    const QString convoId = blogConvoIdForPubkey(pubkeyHex);
    if (m_watchedConvos.contains(convoId)) return;
    m_watchedConvos.insert(convoId);

    if (!m_chat) return;

    // Request conversation history; the module returns a JSON array of messages:
    // [{"sender": "<pubkey>", "content": "<hex>", "timestamp": "..."}, ...]
    const QVariant result = m_chat->invokeRemoteMethod(
        "chat_module", "getConversation", convoId);
    const QString historyJson = result.toString();
    if (historyJson.isEmpty()) return;

    const QJsonArray messages = QJsonDocument::fromJson(historyJson.toUtf8()).array();
    for (const auto& msg : messages) {
        const QJsonObject m = msg.toObject();
        const QString sender     = m["sender"].toString();
        const QString contentHex = m["content"].toString();
        if (sender.isEmpty() || contentHex.isEmpty()) continue;
        onChatMessage(convoId, sender, contentHex);
    }
}

void ChatSync::unsubscribeFromAuthor(const QString& pubkeyHex)
{
    if (pubkeyHex.isEmpty()) return;
    m_watchedConvos.remove(blogConvoIdForPubkey(pubkeyHex));
}

void ChatSync::onChatMessage(const QString& convoId,
                             const QString& senderPubkey,
                             const QString& contentHex)
{
    if (!m_watchedConvos.contains(convoId)) return;
    if (senderPubkey.isEmpty() || contentHex.isEmpty()) return;

    // Decode hex-encoded envelope JSON
    const QByteArray raw = QByteArray::fromHex(contentHex.toLatin1());
    if (raw.isEmpty()) return;

    const QString envelopeJson = QString::fromUtf8(raw);
    if (envelopeJson.isEmpty()) return;

    emit messageReceived(senderPubkey, envelopeJson);
}
