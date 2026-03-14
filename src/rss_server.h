#pragma once
#include <QObject>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>

class QTcpServer;
class QTcpSocket;
class PostStore;
class FeedStore;

// Phase 5+ implementation. Stub for Phase 1/2.
class RssServer : public QObject {
    Q_OBJECT
public:
    explicit RssServer(QObject* parent = nullptr);

    void setPostStore(PostStore* posts);
    void setFeedStore(FeedStore* feed);

    bool    start(const QString& bindAddress = "127.0.0.1", int port = 8484);
    void    stop();
    bool    isRunning() const;
    int     port() const;
    QString bindAddress() const;

signals:
    void started(int port);
    void stopped();
    void requestServed(const QString& path, int statusCode);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer* m_server = nullptr;
    PostStore*  m_posts  = nullptr;
    FeedStore*  m_feed   = nullptr;
    QString     m_bind   = "127.0.0.1";
    int         m_port   = 8484;

    struct Response { int status; QString contentType; QByteArray body; };

    void handleRequest(QTcpSocket* socket, const QString& method,
                       const QString& path, const QMap<QString,QString>& headers);

    Response serveHealth();
    Response serveAggregatedFeed();
    Response serveAuthorFeed(const QString& pubkeyHex);
    Response serveMyFeed();
    Response serveOpmlExport();
    Response serveNotFound();

    QByteArray buildAtomFeed(const QString& feedId, const QString& title,
                             const QString& selfUrl, const QJsonArray& posts,
                             const QString& authorName = QString());
    QString    postToAtomEntry(const QJsonObject& post, const QString& authorPubkey,
                               const QString& authorName);
    QString    markdownToHtml(const QString& markdown);
    QString    isoNow();
    QString    etagFor(const QByteArray& body);
    void       sendResponse(QTcpSocket* socket, const Response& resp,
                            const QMap<QString,QString>& requestHeaders);
};
