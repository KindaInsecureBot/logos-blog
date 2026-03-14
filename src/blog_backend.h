#pragma once
#include <QObject>
#include <QStringList>

class LogosAPI;
class ModuleProxy;

class BlogBackend : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString  ownPubkey      READ ownPubkey      NOTIFY identityChanged)
    Q_PROPERTY(QString  displayName    READ displayName    NOTIFY identityChanged)
    Q_PROPERTY(QString  bio            READ bio            NOTIFY identityChanged)
    Q_PROPERTY(bool     rssRunning     READ rssRunning     NOTIFY rssStateChanged)
    Q_PROPERTY(int      rssPort        READ rssPort        NOTIFY rssStateChanged)
    Q_PROPERTY(QString  rssBindAddress READ rssBindAddress NOTIFY rssStateChanged)
    Q_PROPERTY(bool     wakuConnected  READ wakuConnected  NOTIFY wakuStateChanged)

public:
    explicit BlogBackend(QObject* parent = nullptr);

    void initLogos(LogosAPI* api);

    // Property getters
    QString ownPubkey()      const;
    QString displayName()    const;
    QString bio()            const;
    bool    rssRunning()     const;
    int     rssPort()        const;
    QString rssBindAddress() const;
    bool    wakuConnected()  const;

    // Invokable from QML — Identity
    Q_INVOKABLE void setIdentity(const QString& displayName, const QString& bio);

    // Posts — return JSON strings (QML calls JSON.parse())
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

    // Feed
    Q_INVOKABLE bool    subscribe(const QString& pubkey, const QString& displayName);
    Q_INVOKABLE bool    unsubscribe(const QString& pubkey);
    Q_INVOKABLE QString listSubscriptions();
    Q_INVOKABLE QString getAggregatedFeed();
    Q_INVOKABLE QString getFeedByAuthor(const QString& pubkey);

    // Settings
    Q_INVOKABLE bool setRssPort(int port);
    Q_INVOKABLE bool setRssBindAddress(const QString& address);

signals:
    void identityChanged();
    void rssStateChanged();
    void wakuStateChanged();
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void errorOccurred(const QString& message);

private:
    ModuleProxy* m_blogModule    = nullptr;
    LogosAPI*    m_api           = nullptr;

    QString m_ownPubkey;
    QString m_displayName;
    QString m_bio;
    bool    m_rssRunning     = false;
    int     m_rssPort        = 8484;
    QString m_rssBindAddress = "127.0.0.1";
    bool    m_wakuConnected  = false;

    void connectSignals();
    void refreshIdentity();
    void refreshRssState();
};
