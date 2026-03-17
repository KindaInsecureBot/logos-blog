#pragma once
#include "plugin_interface.h"
#include "post_store.h"
#include "feed_store.h"
#include "chat_sync.h"
#include "rss_server.h"
#include "crypto.h"
#include <QtPlugin>

class BlogPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BlogPlugin(QObject* parent = nullptr);

    Q_INVOKABLE QString version() const override { return "0.2.0"; }
    Q_INVOKABLE void    initLogos(LogosAPI* api) override;

    // Identity
    Q_INVOKABLE QString getIdentity();
    Q_INVOKABLE bool    setIdentity(const QString& displayName, const QString& bio);
    Q_INVOKABLE QString generateKeypair();

    // Post management
    Q_INVOKABLE QString createPost(const QString& title, const QString& body,
                                   const QString& summary, const QStringList& tags);
    Q_INVOKABLE bool    updatePost(const QString& id, const QString& title,
                                   const QString& body, const QString& summary,
                                   const QStringList& tags);
    Q_INVOKABLE bool    publishPost(const QString& id);
    Q_INVOKABLE bool    deletePost(const QString& id);
    Q_INVOKABLE QString getPost(const QString& id);
    Q_INVOKABLE QString listPosts();
    Q_INVOKABLE QString listDrafts();

    // Feed / subscriptions
    Q_INVOKABLE bool    subscribe(const QString& pubkey, const QString& displayName);
    Q_INVOKABLE bool    unsubscribe(const QString& pubkey);
    Q_INVOKABLE QString listSubscriptions();
    Q_INVOKABLE QString getFeedPosts(const QString& pubkey);
    Q_INVOKABLE QString getAggregatedFeed();

    // RSS server
    Q_INVOKABLE int     getRssPort();
    Q_INVOKABLE bool    setRssPort(int port);
    Q_INVOKABLE QString getRssBindAddress();
    Q_INVOKABLE bool    setRssBindAddress(const QString& address);

    // Search and filtering
    Q_INVOKABLE QString searchPosts(const QString& query);
    Q_INVOKABLE QString getPostsByTag(const QString& tag);

    // OPML import/export
    Q_INVOKABLE QString getOpmlContent();
    Q_INVOKABLE bool    importOpml(const QString& xml);

    // LEZ registry: returns JSON array of CIDs for an author from on-chain registry
    Q_INVOKABLE QString getRegistryCids(const QString& authorPubkey);

signals:
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void subscriptionAdded(const QString& pubkey);
    void identityChanged();
    void chatStarted();

private:
    LogosAPI*    m_api      = nullptr;
    PostStore*   m_posts    = nullptr;
    FeedStore*   m_feed     = nullptr;
    ChatSync*    m_chatSync = nullptr;
    RssServer*   m_rss      = nullptr;

    ModuleProxy* m_kv          = nullptr;
    ModuleProxy* m_storage     = nullptr;  // org.logos.StorageModuleInterface
    ModuleProxy* m_chat        = nullptr;  // org.logos.ChatSDKModuleInterface
    ModuleProxy* m_lezRegistry = nullptr;  // lez_registry_module (SPEL program)

    // Cached identity fields — set during loadOrCreateIdentity
    QString m_ownPubkey;
    QString m_ownPrivkey;
    QString m_displayName;

    void loadOrCreateIdentity();
    void startRssServer();
    void connectChatModule();

    // Fetch post content from Storage by CID; returns empty string on failure
    QString fetchFromStorage(const QString& cid);

    // Inscribe a CID in the on-chain LEZ registry
    void inscribeInRegistry(const QString& cid);

    // Build a compact signed CID notification envelope for Chat delivery.
    // type is "post", "delete", or "profile".
    QString buildSignedEnvelope(const QString& type, const QJsonObject& typePayload);
};
