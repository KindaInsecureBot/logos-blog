#include "chat_sync.h"
#include "logos_api_client.h"

#include <QDateTime>

ChatSync::ChatSync(QObject* parent)
    : QObject(parent)
{}

void ChatSync::setChatClient(LogosAPIClient* chat)
{
    m_chat = chat;
}

void ChatSync::setOwnPubkey(const QString& pubkeyHex)
{
    m_ownPubkey = pubkeyHex;
}

void ChatSync::start()
{
    if (!m_chat) {
        // No Chat SDK client — report as started so the rest of the plugin
        // can continue in degraded mode (local-only, no P2P).
        emit chatStarted();
        return;
    }

    // Initialize Chat SDK with default blog configuration.
    const QString config = R"({"logLevel":"WARN","preset":"logos.dev"})";
    m_chat->invokeRemoteMethod("chatsdk_module", "initChat", config);
    m_chat->invokeRemoteMethod("chatsdk_module", "startChat");

    // Join own blog convoId so outbound messages are reflected back for
    // consistency (and so the send path has a joined conversation).
    if (!m_ownPubkey.isEmpty()) {
        subscribeToAuthor(m_ownPubkey);
    }

    emit chatStarted();
}

void ChatSync::sendPost(const QString& signedEnvelopeJson)
{
    if (!m_chat || m_ownPubkey.isEmpty()) return;
    const QString convoId    = convoIdForPubkey(m_ownPubkey);
    const QString hexPayload = QString::fromLatin1(
        signedEnvelopeJson.toUtf8().toHex());
    m_chat->invokeRemoteMethod("chatsdk_module", "sendMessage", convoId, hexPayload);
}

void ChatSync::sendDelete(const QString& postId)
{
    if (!m_chat || m_ownPubkey.isEmpty()) return;
    const QString convoId = convoIdForPubkey(m_ownPubkey);
    const QString envelope =
        QStringLiteral(R"({"version":1,"type":"delete","post_id":")") + postId + "\"}";
    const QString hexPayload = QString::fromLatin1(envelope.toUtf8().toHex());
    m_chat->invokeRemoteMethod("chatsdk_module", "sendMessage", convoId, hexPayload);
}

void ChatSync::sendProfile(const QString& displayName, const QString& bio)
{
    if (!m_chat || m_ownPubkey.isEmpty()) return;
    const QString convoId = convoIdForPubkey(m_ownPubkey);
    const QString envelope =
        QStringLiteral(R"({"version":1,"type":"profile","name":")") +
        displayName + R"(","bio":")" + bio + "\"}";
    const QString hexPayload = QString::fromLatin1(envelope.toUtf8().toHex());
    m_chat->invokeRemoteMethod("chatsdk_module", "sendMessage", convoId, hexPayload);
}

void ChatSync::subscribeToAuthor(const QString& pubkeyHex)
{
    if (!m_chat || pubkeyHex.isEmpty()) return;
    const QString convoId = convoIdForPubkey(pubkeyHex);
    if (m_joinedConvos.contains(convoId)) return;
    m_joinedConvos.insert(convoId);
    // Join the author's blog broadcast conversation.
    // NOTE: The Chat SDK public-channel / group API is not yet finalised.
    //   joinConversation may require a prior intro-bundle exchange for private
    //   conversations. For public blog broadcasts we treat convoId as a
    //   content topic similar to the old Waku approach.
    // TODO: Replace with the Chat SDK group/public-channel join call once the
    //   API is stable (see https://github.com/logos-co/status-go ChatSDK docs).
    m_chat->invokeRemoteMethod("chatsdk_module", "joinConversation", convoId);
}

void ChatSync::unsubscribeFromAuthor(const QString& pubkeyHex)
{
    if (!m_chat || pubkeyHex.isEmpty()) return;
    const QString convoId = convoIdForPubkey(pubkeyHex);
    m_joinedConvos.remove(convoId);
    m_chat->invokeRemoteMethod("chatsdk_module", "leaveConversation", convoId);
}

void ChatSync::requestHistory(const QString& pubkeyHex, const QDateTime& since)
{
    if (!m_chat || pubkeyHex.isEmpty()) return;
    const QString convoId  = convoIdForPubkey(pubkeyHex);
    const QString sinceIso = since.toUTC().toString(Qt::ISODate);
    // TODO: Replace with the actual Chat SDK history query method name once
    //   the SDK API is finalised. "queryHistory" is a best-effort call.
    m_chat->invokeRemoteMethod("chatsdk_module", "queryHistory", convoId, sinceIso);
}

void ChatSync::onChatMessage(const QString& convoId, const QString& hexPayload)
{
    // Decode hex → UTF-8 JSON envelope.
    const QByteArray raw = QByteArray::fromHex(hexPayload.toLatin1());
    emit messageReceived(convoId, QString::fromUtf8(raw));
}

// static
QString ChatSync::convoIdForPubkey(const QString& pubkeyHex)
{
    // Stable, human-readable conversation ID for the author's blog channel.
    return QStringLiteral("logos-blog:") + pubkeyHex;
}
