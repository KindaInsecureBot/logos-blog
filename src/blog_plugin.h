#pragma once
#include "plugin_interface.h"
#include "post_store.h"
#include "feed_store.h"
#include "waku_sync.h"
#include "rss_server.h"
#include <QtPlugin>

class BlogPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BlogPlugin(QObject* parent = nullptr);

    Q_INVOKABLE QString version() const override { return "0.1.0"; }
    Q_INVOKABLE void    initLogos(LogosAPI* api) override;

    // Identity
    Q_INVOKABLE QString getIdentity();
    Q_INVOKABLE bool    setIdentity(const QString& displayName, const QString& bio);

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

signals:
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void subscriptionAdded(const QString& pubkey);
    void identityChanged();

private:
    LogosAPI*    m_api   = nullptr;
    PostStore*   m_posts = nullptr;
    FeedStore*   m_feed  = nullptr;
    WakuSync*    m_waku  = nullptr;
    RssServer*   m_rss   = nullptr;

    ModuleProxy* m_kv       = nullptr;
    ModuleProxy* m_delivery = nullptr;

    void loadOrCreateIdentity();
    void startRssServer();
};
