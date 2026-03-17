#pragma once
#include <QObject>
#include <QSet>

class ModuleProxy;

// Chat SDK module wrapper — replaces WakuSync for P2P blog message delivery.
//
// Each author broadcasts post CID notifications to their deterministic "blog
// channel" conversation.  The conversation ID is derived as:
//   SHA-256("logos-blog-channel:" + pubkeyHex)  → hex string
//
// Messages sent to a conversation are hex-encoded UTF-8 JSON envelopes.
// The Chat SDK module ID is "org.logos.ChatSDKModuleInterface".
class ChatSync : public QObject {
    Q_OBJECT
public:
    explicit ChatSync(QObject* parent = nullptr);

    void setChatClient(ModuleProxy* chat);
    void setOwnPubkey(const QString& pubkeyHex);

    // Initialise and start the Chat SDK node. Emits chatStarted() on success.
    void start();

    // Send a signed envelope JSON to own blog channel (hex-encoded on the wire).
    void publishMessage(const QString& signedEnvelopeJson);

    // Subscribe to an author's blog channel to receive their CID notifications.
    // Fetches any stored history and emits messageReceived for each past entry.
    void subscribeToAuthor(const QString& pubkeyHex);

    // Stop listening to an author's blog channel.
    void unsubscribeFromAuthor(const QString& pubkeyHex);

    // Called by BlogPlugin when chat_module fires its messageReceived signal.
    void onChatMessage(const QString& convoId,
                       const QString& senderPubkey,
                       const QString& contentHex);

    // Derive the blog channel conversation ID for a given pubkey.
    static QString blogConvoIdForPubkey(const QString& pubkeyHex);

signals:
    // Decoded envelope JSON from a subscribed author (or own pubkey).
    void messageReceived(const QString& senderPubkey, const QString& envelopeJson);
    void chatStarted();
    void chatError(const QString& error);

private:
    ModuleProxy*  m_chat = nullptr;
    QString       m_ownPubkey;
    QString       m_ownConvoId;
    QSet<QString> m_watchedConvos; // convo IDs we are actively watching
};
