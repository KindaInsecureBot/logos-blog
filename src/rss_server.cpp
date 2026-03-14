#include "rss_server.h"
#include "post_store.h"
#include "feed_store.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTextStream>
#include <QRegularExpression>

RssServer::RssServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &RssServer::onNewConnection);
}

void RssServer::setPostStore(PostStore* posts) { m_posts = posts; }
void RssServer::setFeedStore(FeedStore* feed)  { m_feed  = feed;  }

bool RssServer::start(const QString& bindAddress, int port)
{
    m_bind = bindAddress;
    m_port = port;

    // Try up to 5 ports if the preferred one is busy
    for (int attempt = 0; attempt < 5; ++attempt) {
        const int tryPort = port + attempt;
        if (m_server->listen(QHostAddress(bindAddress), tryPort)) {
            m_port = tryPort;
            emit started(m_port);
            return true;
        }
    }
    return false;
}

void RssServer::stop()
{
    m_server->close();
    emit stopped();
}

bool    RssServer::isRunning()    const { return m_server->isListening(); }
int     RssServer::port()         const { return m_port; }
QString RssServer::bindAddress()  const { return m_bind; }

void RssServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &RssServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void RssServer::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    const QString request = QString::fromUtf8(socket->readAll());
    const QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) return;

    // Parse request line: "GET /path HTTP/1.1"
    const QStringList requestLine = lines[0].split(' ');
    if (requestLine.size() < 2) return;

    const QString method = requestLine[0];
    const QString path   = requestLine[1];

    // Parse headers
    QMap<QString,QString> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines[i].indexOf(':');
        if (colon > 0) {
            headers[lines[i].left(colon).trimmed().toLower()] =
                lines[i].mid(colon + 1).trimmed();
        }
    }

    handleRequest(socket, method, path, headers);
}

void RssServer::handleRequest(QTcpSocket* socket, const QString& method,
                              const QString& path, const QMap<QString,QString>& headers)
{
    Response resp;

    if (path == "/health") {
        resp = serveHealth();
    } else if (path == "/feed.xml") {
        resp = serveAggregatedFeed();
    } else if (path == "/my/feed.xml") {
        resp = serveMyFeed();
    } else if (path == "/opml") {
        resp = serveOpmlExport();
    } else if (path.startsWith("/@") && path.endsWith("/feed.xml")) {
        // /@<pubkey>/feed.xml
        const QString pubkey = path.mid(2, path.length() - 2 - QString("/feed.xml").length());
        resp = serveAuthorFeed(pubkey);
    } else {
        resp = serveNotFound();
    }

    (void)method; // future: handle POST /opml for import
    sendResponse(socket, resp, headers);
    emit requestServed(path, resp.status);
}

void RssServer::sendResponse(QTcpSocket* socket, const Response& resp,
                             const QMap<QString,QString>& requestHeaders)
{
    const QString etag = etagFor(resp.body);
    const QString clientEtag = requestHeaders.value("if-none-match");

    if (resp.status == 200 && !clientEtag.isEmpty() && clientEtag == etag) {
        // 304 Not Modified
        QByteArray out;
        QTextStream ts(&out);
        ts << "HTTP/1.1 304 Not Modified\r\n"
           << "ETag: " << etag << "\r\n"
           << "Connection: close\r\n\r\n";
        ts.flush();
        socket->write(out);
        socket->disconnectFromHost();
        return;
    }

    QByteArray out;
    QTextStream ts(&out);
    ts << "HTTP/1.1 " << resp.status << " "
       << (resp.status == 200 ? "OK" : resp.status == 404 ? "Not Found" : "Error")
       << "\r\n"
       << "Content-Type: " << resp.contentType << "\r\n"
       << "Content-Length: " << resp.body.size() << "\r\n"
       << "ETag: " << etag << "\r\n"
       << "Cache-Control: max-age=60\r\n"
       << "Connection: close\r\n\r\n";
    ts.flush();
    out.append(resp.body);
    socket->write(out);
    socket->disconnectFromHost();
}

RssServer::Response RssServer::serveHealth()
{
    return {200, "text/plain", "OK"};
}

RssServer::Response RssServer::serveMyFeed()
{
    if (!m_posts) return serveNotFound();

    const QJsonArray posts = m_posts->listPosts();
    const QByteArray atom = buildAtomFeed(
        "urn:logos-blog:local",
        "My Blog",
        QStringLiteral("http://localhost:") + QString::number(m_port) + "/my/feed.xml",
        posts);
    return {200, "application/atom+xml; charset=utf-8", atom};
}

RssServer::Response RssServer::serveAggregatedFeed()
{
    if (!m_feed) return serveNotFound();

    const QJsonArray posts = m_feed->getAggregatedFeed();
    const QByteArray atom = buildAtomFeed(
        "urn:logos-blog:aggregated",
        "Subscriptions",
        QStringLiteral("http://localhost:") + QString::number(m_port) + "/feed.xml",
        posts);
    return {200, "application/atom+xml; charset=utf-8", atom};
}

RssServer::Response RssServer::serveAuthorFeed(const QString& pubkeyHex)
{
    if (!m_feed) return serveNotFound();

    const QJsonArray posts = m_feed->getPostsByAuthor(pubkeyHex);
    const QString selfUrl = QStringLiteral("http://localhost:") + QString::number(m_port) +
                            "/@" + pubkeyHex + "/feed.xml";
    const QByteArray atom = buildAtomFeed(
        QStringLiteral("urn:logos-blog:") + pubkeyHex,
        pubkeyHex.left(16) + "...",
        selfUrl, posts);
    return {200, "application/atom+xml; charset=utf-8", atom};
}

RssServer::Response RssServer::serveOpmlExport()
{
    if (!m_feed) return serveNotFound();

    const QJsonArray subs = m_feed->listSubscriptions();
    QString opml;
    QTextStream ts(&opml);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<opml version=\"2.0\">\n"
       << "  <head><title>Logos Blog Subscriptions</title></head>\n"
       << "  <body>\n";
    for (const auto& s : subs) {
        const QJsonObject sub = s.toObject();
        const QString pubkey = sub["pubkey"].toString();
        const QString name   = sub["name"].toString();
        const QString url = QStringLiteral("http://localhost:") + QString::number(m_port) +
                            "/@" + pubkey + "/feed.xml";
        ts << "    <outline type=\"rss\" text=\"" << name.toHtmlEscaped()
           << "\" xmlUrl=\"" << url << "\" pubkey=\"" << pubkey << "\"/>\n";
    }
    ts << "  </body>\n</opml>\n";
    ts.flush();

    return {200, "text/xml; charset=utf-8", opml.toUtf8()};
}

RssServer::Response RssServer::serveNotFound()
{
    return {404, "text/plain", "Not Found"};
}

QByteArray RssServer::buildAtomFeed(const QString& feedId, const QString& title,
                                     const QString& selfUrl, const QJsonArray& posts,
                                     const QString& authorName)
{
    QString xml;
    QTextStream ts(&xml);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
       << "  <title>" << title.toHtmlEscaped() << "</title>\n"
       << "  <id>" << feedId << "</id>\n"
       << "  <link href=\"" << selfUrl << "\" rel=\"self\"/>\n"
       << "  <updated>" << isoNow() << "</updated>\n";

    if (!authorName.isEmpty()) {
        ts << "  <author><name>" << authorName.toHtmlEscaped() << "</name></author>\n";
    }

    for (const auto& p : posts) {
        const QJsonObject post = p.toObject();
        const QString pubkey = post.contains("author_pubkey")
            ? post["author_pubkey"].toString()
            : QString();
        const QString aName = post.contains("author_name")
            ? post["author_name"].toString()
            : authorName;
        ts << postToAtomEntry(post, pubkey, aName);
    }

    ts << "</feed>\n";
    ts.flush();
    return xml.toUtf8();
}

QString RssServer::postToAtomEntry(const QJsonObject& post, const QString& authorPubkey,
                                    const QString& authorName)
{
    const QString id      = post["id"].toString();
    const QString title   = post["title"].toString();
    const QString body    = post["body"].toString();
    const QString summary = post["summary"].toString();
    const QString created = post["created_at"].toString();
    const QString updated = post["updated_at"].toString();
    const QJsonArray tags = post["tags"].toArray();

    const QString entryId = authorPubkey.isEmpty()
        ? QStringLiteral("urn:logos-blog:") + id
        : QStringLiteral("urn:logos-blog:") + authorPubkey + ":" + id;

    QString entry;
    QTextStream ts(&entry);
    ts << "  <entry>\n"
       << "    <title>" << title.toHtmlEscaped() << "</title>\n"
       << "    <id>" << entryId << "</id>\n"
       << "    <published>" << created << "</published>\n"
       << "    <updated>" << updated << "</updated>\n";

    if (!summary.isEmpty()) {
        ts << "    <summary>" << summary.toHtmlEscaped() << "</summary>\n";
    }
    if (!authorName.isEmpty()) {
        ts << "    <author><name>" << authorName.toHtmlEscaped() << "</name></author>\n";
    }

    ts << "    <content type=\"html\"><![CDATA[" << markdownToHtml(body) << "]]></content>\n";

    for (const auto& tag : tags) {
        ts << "    <category term=\"" << tag.toString().toHtmlEscaped() << "\"/>\n";
    }

    ts << "  </entry>\n";
    ts.flush();
    return entry;
}

// Minimal MVP markdown → HTML (Phase 6 replaces with cmark)
QString RssServer::markdownToHtml(const QString& markdown)
{
    QString html = markdown.toHtmlEscaped();
    // Headers
    html.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption),
                 "<h3>\\1</h3>");
    html.replace(QRegularExpression("^## (.+)$", QRegularExpression::MultilineOption),
                 "<h2>\\1</h2>");
    html.replace(QRegularExpression("^# (.+)$", QRegularExpression::MultilineOption),
                 "<h1>\\1</h1>");
    // Bold and italic
    html.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");
    html.replace(QRegularExpression("\\*(.+?)\\*"), "<em>\\1</em>");
    // Line breaks
    html.replace("\n\n", "</p><p>");
    html.replace("\n", "<br/>");
    return "<p>" + html + "</p>";
}

QString RssServer::isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString RssServer::etagFor(const QByteArray& body)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex());
}
