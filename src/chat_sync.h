#pragma once
#include <QObject>
#include <QSet>
#include <QDateTime>

class LogosAPIClient;

// Chat SDK integration for blog P2P messaging (org.logos.ChatSDKModuleInterface).
// Replaces WakuSync / delivery_module: blog posts are now broadcast as Chat SDK
// messages whose payload is a hex-encoded signed envelope containing a storage CID.
//
// Publishing flow:
//   1. BlogPlugin uploads post to StorageSync → CID
//   2. BlogPlugin calls buildSignedEnvelope() with cid field
//   3. ChatSync::sendPost(signedEnvelopeJson) sends via sendMessage(convoId, hexPayload)
//
// Receiving flow:
//   1. Chat SDK fires incoming message event (onEvent "messageReceived")
//   2. ChatSync::onChatMessage(convoId, hexPayload) decodes and emits messageReceived
//   3. BlogPlugin fetches body from StorageSync, feeds FeedStore
//
// Conversation IDs:
//   Blog uses a stable convoId derived from the author pubkey so each author has
//   a dedicated broadcast "channel". The format is "logos-blog:<pubkey-hex>".
//   This mirrors the old Waku topic structure for kv-index compatibility.
class ChatSync : public QObject {
    Q_OBJECT
public:
    explicit ChatSync(QObject* parent = nullptr);

    void setChatClient(LogosAPIClient* chat);
    void setOwnPubkey(const QString& pubkeyHex);

    // Initialize Chat SDK and start chat. Subscribes own blog convoId.
    void start();

    // Broadcast a signed post envelope (containing CID) on the own blog convoId.
    void sendPost(const QString& signedEnvelopeJson);

    // Broadcast a delete tombstone on the own blog convoId.
    void sendDelete(const QString& postId);

    // Broadcast a profile update on the own blog convoId.
    void sendProfile(const QString& displayName, const QString& bio);

    // Join an author's blog conversation to receive their posts.
    void subscribeToAuthor(const QString& pubkeyHex);

    // Leave an author's blog conversation.
    void unsubscribeFromAuthor(const QString& pubkeyHex);

    // Request history replay for an author's convoId (best-effort).
    // TODO: Update when Chat SDK exposes a getMessages/queryHistory method.
    void requestHistory(const QString& pubkeyHex, const QDateTime& since);

    // Called by BlogPlugin when the Chat SDK fires an incoming message event.
    // Decodes hex payload and emits messageReceived.
    void onChatMessage(const QString& convoId, const QString& hexPayload);

signals:
    // Emitted with the convoId and decoded JSON payload string.
    void messageReceived(const QString& convoId, const QString& payloadJson);
    void chatStarted();
    void chatError(const QString& error);

private:
    LogosAPIClient* m_chat = nullptr;
    QString         m_ownPubkey;
    QSet<QString>   m_joinedConvos;

    // Stable convoId for a given author pubkey.
    static QString convoIdForPubkey(const QString& pubkeyHex);
};
