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
#include <QXmlStreamReader>

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

    const QByteArray raw = socket->readAll();

    // Split headers and body at the blank line
    const int sep = raw.indexOf("\r\n\r\n");
    const QByteArray headersPart = sep >= 0 ? raw.left(sep)      : raw;
    const QByteArray body        = sep >= 0 ? raw.mid(sep + 4)   : QByteArray();

    const QString headerStr = QString::fromUtf8(headersPart);
    const QStringList lines = headerStr.split("\r\n");
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

    handleRequest(socket, method, path, headers, body);
}

void RssServer::handleRequest(QTcpSocket* socket, const QString& method,
                              const QString& path, const QMap<QString,QString>& headers,
                              const QByteArray& body)
{
    Response resp;

    if (path == "/health") {
        resp = serveHealth();
    } else if (path == "/") {
        resp = serveIndex();
    } else if (path == "/feed.xml") {
        resp = serveAggregatedFeed();
    } else if (path == "/my/feed.xml") {
        resp = serveMyFeed();
    } else if (path == "/opml") {
        if (method == "POST") {
            resp = importOpml(body);
        } else {
            resp = serveOpmlExport();
        }
    } else if (path.startsWith("/@") && path.endsWith("/feed.xml")) {
        // /@<pubkey>/feed.xml
        const QString pubkey = path.mid(2, path.length() - 2 - QString("/feed.xml").length());
        resp = serveAuthorFeed(pubkey);
    } else {
        resp = serveNotFound();
    }

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
    return {200, "application/json; charset=utf-8", R"({"status":"ok"})"};
}

RssServer::Response RssServer::serveIndex()
{
    const QString base = QStringLiteral("http://") + m_bind + ":" + QString::number(m_port);
    QString html;
    QTextStream ts(&html);
    ts << "<!DOCTYPE html><html><head>"
       << "<meta charset=\"utf-8\"/>"
       << "<title>Logos Blog RSS Bridge</title>"
       << "<style>body{font-family:sans-serif;background:#1a1b1e;color:#c1c2c5;padding:2em}"
       << "a{color:#4dabf7}h1{color:#fff}ul{list-style:none;padding:0}"
       << "li{margin:.5em 0}code{background:#25262b;padding:.2em .4em;border-radius:3px}</style>"
       << "</head><body>"
       << "<h1>Logos Blog RSS Bridge</h1>"
       << "<p>Running on port " << m_port << "</p>"
       << "<ul>"
       << "<li><a href=\"/my/feed.xml\">My Feed</a> — <code>" << base << "/my/feed.xml</code></li>"
       << "<li><a href=\"/feed.xml\">Subscriptions Feed</a> — <code>" << base << "/feed.xml</code></li>"
       << "<li><a href=\"/opml\">OPML Export</a> — <code>" << base << "/opml</code></li>"
       << "<li><a href=\"/health\">Health Check</a> — <code>" << base << "/health</code></li>"
       << "</ul>"
       << "<p>To import subscriptions: <code>POST /opml</code> with OPML body.</p>"
       << "</body></html>";
    ts.flush();
    return {200, "text/html; charset=utf-8", html.toUtf8()};
}

RssServer::Response RssServer::serveMyFeed()
{
    if (!m_posts) return serveNotFound();

    const QString base = QStringLiteral("http://") + m_bind + ":" + QString::number(m_port);
    const QJsonArray posts = m_posts->listPosts();
    const QByteArray atom = buildAtomFeed(
        "urn:logos-blog:local",
        "My Blog",
        base + "/my/feed.xml",
        posts);
    return {200, "application/atom+xml; charset=utf-8", atom};
}

RssServer::Response RssServer::serveAggregatedFeed()
{
    if (!m_feed) return serveNotFound();

    const QString base = QStringLiteral("http://") + m_bind + ":" + QString::number(m_port);
    const QJsonArray posts = m_feed->getAggregatedFeed();
    const QByteArray atom = buildAtomFeed(
        "urn:logos-blog:aggregated",
        "Subscriptions",
        base + "/feed.xml",
        posts);
    return {200, "application/atom+xml; charset=utf-8", atom};
}

RssServer::Response RssServer::serveAuthorFeed(const QString& pubkeyHex)
{
    if (!m_feed) return serveNotFound();

    const QString base    = QStringLiteral("http://") + m_bind + ":" + QString::number(m_port);
    const QString selfUrl = base + "/@" + pubkeyHex + "/feed.xml";
    const QJsonArray posts = m_feed->getPostsByAuthor(pubkeyHex);
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
    const QString base = QStringLiteral("http://") + m_bind + ":" + QString::number(m_port);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<opml version=\"2.0\">\n"
       << "  <head>\n"
       << "    <title>Logos Blog Subscriptions</title>\n"
       << "    <dateCreated>"
       << QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date)
       << "</dateCreated>\n"
       << "  </head>\n"
       << "  <body>\n";
    for (const auto& s : subs) {
        const QJsonObject sub = s.toObject();
        const QString pubkey = sub["pubkey"].toString();
        const QString name   = sub["name"].toString();
        const QString feedUrl = base + "/@" + pubkey + "/feed.xml";
        const QString htmlUrl = base + "/@" + pubkey;
        ts << "    <outline type=\"rss\" text=\"" << name.toHtmlEscaped()
           << "\" xmlUrl=\"" << feedUrl
           << "\" htmlUrl=\"" << htmlUrl
           << "\" pubkey=\"" << pubkey << "\"/>\n";
    }
    ts << "  </body>\n</opml>\n";
    ts.flush();

    return {200, "text/xml; charset=utf-8", opml.toUtf8()};
}

RssServer::Response RssServer::serveNotFound()
{
    return {404, "text/plain", "Not Found"};
}

RssServer::Response RssServer::importOpml(const QByteArray& body)
{
    if (!m_feed) return {503, "text/plain", "Feed store not available"};
    if (body.isEmpty()) return {400, "text/plain", "Empty body"};

    static const QRegularExpression pubkeyFromUrl(R"(/@([0-9a-f]{64})/feed\.xml)");

    QXmlStreamReader xml(body);
    int imported = 0;
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.tokenType() != QXmlStreamReader::StartElement) continue;
        if (xml.name() != QStringView(u"outline")) continue;

        const QXmlStreamAttributes attrs = xml.attributes();
        const QString xmlUrl = attrs.value("xmlUrl").toString();
        const QString name   = attrs.value("text").toString();

        // Prefer explicit pubkey attribute, fall back to URL extraction
        QString pubkey = attrs.value("pubkey").toString();
        if (pubkey.isEmpty() && !xmlUrl.isEmpty()) {
            const QRegularExpressionMatch m = pubkeyFromUrl.match(xmlUrl);
            if (m.hasMatch()) pubkey = m.captured(1);
        }
        if (pubkey.isEmpty()) continue;

        const QString displayName = name.isEmpty() ? pubkey.left(8) : name;
        m_feed->subscribe(pubkey, displayName);
        ++imported;
    }

    if (xml.hasError())
        return {400, "application/json",
                QStringLiteral("{\"error\":\"XML parse error\"}").toUtf8()};

    const QByteArray resp = QStringLiteral("{\"imported\":%1}").arg(imported).toUtf8();
    return {200, "application/json", resp};
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

// Regex-based markdown → HTML (Phase 6 improved version)
QString RssServer::markdownToHtml(const QString& markdown)
{
    // ── Step 1: Extract fenced code blocks before HTML escaping ──────────────
    // Protects code content from further processing
    QStringList codeBlockHtml;
    QString processed = markdown;

    {
        static const QRegularExpression fencedCode(
            "```(\\w*)\\n([\\s\\S]*?)```",
            QRegularExpression::MultilineOption);

        QRegularExpressionMatchIterator it = fencedCode.globalMatch(processed);
        QStringList parts;
        int lastPos = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            parts << processed.mid(lastPos, m.capturedStart() - lastPos);
            const QString lang = m.captured(1);
            const QString code = m.captured(2);
            QString block = "<pre><code";
            if (!lang.isEmpty())
                block += " class=\"language-" + lang.toHtmlEscaped() + "\"";
            block += ">" + code.toHtmlEscaped() + "</code></pre>";
            parts << QStringLiteral("\x01CODEBLOCK%1\x01").arg(codeBlockHtml.size());
            codeBlockHtml << block;
            lastPos = m.capturedEnd();
        }
        parts << processed.mid(lastPos);
        processed = parts.join(QString());
    }

    // ── Step 2: HTML-escape everything except placeholders ──────────────────
    processed = processed.toHtmlEscaped();

    // ── Step 3: Restore code block placeholders ──────────────────────────────
    for (int i = 0; i < codeBlockHtml.size(); ++i)
        processed.replace(QStringLiteral("\x01CODEBLOCK%1\x01").arg(i), codeBlockHtml[i]);

    // ── Step 4: Block-level transformations ──────────────────────────────────

    // Blockquotes: ">" becomes "&gt;" after escaping — match that
    processed.replace(
        QRegularExpression("^&gt; (.+)$", QRegularExpression::MultilineOption),
        "<blockquote>\\1</blockquote>");

    // Horizontal rule
    processed.replace(
        QRegularExpression("^---+$", QRegularExpression::MultilineOption),
        "<hr/>");

    // ATX headings (h1–h4)
    processed.replace(QRegularExpression("^#### (.+)$", QRegularExpression::MultilineOption), "<h4>\\1</h4>");
    processed.replace(QRegularExpression("^### (.+)$",  QRegularExpression::MultilineOption), "<h3>\\1</h3>");
    processed.replace(QRegularExpression("^## (.+)$",   QRegularExpression::MultilineOption), "<h2>\\1</h2>");
    processed.replace(QRegularExpression("^# (.+)$",    QRegularExpression::MultilineOption), "<h1>\\1</h1>");

    // Unordered list items: "- item" or "* item"
    processed.replace(
        QRegularExpression("^[\\-\\*] (.+)$", QRegularExpression::MultilineOption),
        "<li>\\1</li>");

    // ── Step 5: Inline transformations ───────────────────────────────────────

    // Images before links (more specific pattern first)
    // ![alt](url) — after escaping, & in URLs becomes &amp; but that is rare in markdown
    processed.replace(QRegularExpression("!\\[([^\\]]*)\\]\\(([^)]+)\\)"),
                      "<img src=\"\\2\" alt=\"\\1\"/>");

    // Links: [text](url)
    processed.replace(QRegularExpression("\\[([^\\]]+)\\]\\(([^)]+)\\)"),
                      "<a href=\"\\2\">\\1</a>");

    // Bold+italic: ***text***
    processed.replace(QRegularExpression("\\*\\*\\*(.+?)\\*\\*\\*"),
                      "<strong><em>\\1</em></strong>");

    // Bold: **text**
    processed.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");

    // Italic: *text*
    processed.replace(QRegularExpression("\\*(.+?)\\*"), "<em>\\1</em>");

    // Inline code: `code`
    processed.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");

    // ── Step 6: Paragraph wrapping ───────────────────────────────────────────
    processed.replace("\n\n", "</p><p>");
    processed.replace("\n", "<br/>");

    return "<p>" + processed + "</p>";
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
