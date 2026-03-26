#pragma once
#include "core/interface.h"
#include "post_store.h"
#include "feed_store.h"
#include "waku_sync.h"
#include "rss_server.h"
#include "crypto.h"
#include <QtPlugin>

class LogosAPIClient;

class BlogPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.BlogModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BlogPlugin(QObject* parent = nullptr);

    Q_INVOKABLE QString name()    const override { return "blog"; }
    Q_INVOKABLE QString version() const override { return "0.1.0"; }
    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // Identity
    Q_INVOKABLE QString getIdentity();
    Q_INVOKABLE bool    setIdentity(const QString& displayName, const QString& bio);
    Q_INVOKABLE QString generateKeypair();   // Generate and persist a new Ed25519 keypair

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

    // Search and filtering (Phase 6)
    Q_INVOKABLE QString searchPosts(const QString& query);
    Q_INVOKABLE QString getPostsByTag(const QString& tag);

    // OPML content and import (Phase 6)
    Q_INVOKABLE QString getOpmlContent();
    Q_INVOKABLE bool    importOpml(const QString& xml);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void subscriptionAdded(const QString& pubkey);
    void identityChanged();
    void wakuStarted();

private:
    LogosAPI*    m_api   = nullptr;
    PostStore*   m_posts = nullptr;
    FeedStore*   m_feed  = nullptr;
    WakuSync*    m_waku  = nullptr;
    RssServer*   m_rss   = nullptr;

    LogosAPIClient* m_kv       = nullptr;
    LogosAPIClient* m_delivery = nullptr;

    // Cached identity fields — set during loadOrCreateIdentity
    QString m_ownPubkey;
    QString m_ownPrivkey;
    QString m_displayName;

    void loadOrCreateIdentity();
    void startRssServer();
    void connectDeliveryModule();

    // Build a signed Waku envelope of the given type.
    // typePayload holds the type-specific field (e.g. {"post": {...}}).
    QString buildSignedEnvelope(const QString& type, const QJsonObject& typePayload);
};
