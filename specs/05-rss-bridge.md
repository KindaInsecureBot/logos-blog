# Logos Blog — RSS Bridge

## Design

`RssServer` is a minimal HTTP/1.1 server running inside `blog_module` (headless plugin). It uses `QTcpServer` + `QTcpSocket` directly — no external HTTP library dependency.

The server runs on localhost by default. The bind address is configurable so power users can expose it to their LAN or behind a reverse proxy.

```
blog_module (logos_host)
  └── RssServer
        ├── QTcpServer  (listens on 127.0.0.1:8484)
        ├── PostStore   (own posts → /my/feed.xml)
        └── FeedStore   (cached subscriptions → /@<pubkey>/feed.xml, /feed.xml)
```

---

## Endpoints

| Path | Description |
|------|-------------|
| `GET /feed.xml` | Aggregated Atom feed of all subscribed authors |
| `GET /@<pubkey>/feed.xml` | Single author's feed (own or subscribed) |
| `GET /my/feed.xml` | Own published posts as Atom feed |
| `GET /opml` | OPML export of all subscriptions |
| `POST /opml` | OPML import (body: XML) |
| `GET /` | HTML index with links to available feeds |
| `GET /health` | `200 OK` — for liveness checks |

All other paths return `404 Not Found`.

---

## HTTP/1.1 Implementation

### Connection Handling

`RssServer` accepts one connection at a time per `QTcpSocket::readyRead` signal. Each request is read completely before responding; responses are sent and the socket is closed (no keep-alive in MVP).

```cpp
void RssServer::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    QByteArray data = socket->readAll();

    // Parse request line + headers
    QString method, path;
    QMap<QString, QString> headers;
    parseHttpRequest(data, method, path, headers);

    Response resp = dispatch(method, path, headers);
    sendHttpResponse(socket, resp);
    socket->disconnectFromHost();
}
```

### Request Parsing

```cpp
void RssServer::parseHttpRequest(const QByteArray& raw,
                                  QString& method, QString& path,
                                  QMap<QString,QString>& headers) {
    QList<QByteArray> lines = raw.split('\n');
    // First line: "GET /feed.xml HTTP/1.1"
    QList<QByteArray> requestLine = lines[0].simplified().split(' ');
    method = requestLine.value(0);
    path   = requestLine.value(1);

    // Header lines: "Host: localhost:8484"
    for (int i = 1; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty()) break;
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        headers[line.left(colon).toLower().trimmed()] =
            line.mid(colon + 1).trimmed();
    }
}
```

### Response Sending

```cpp
void RssServer::sendHttpResponse(QTcpSocket* socket, const Response& resp) {
    QByteArray statusText = resp.status == 200 ? "OK"
                          : resp.status == 304 ? "Not Modified"
                          : resp.status == 404 ? "Not Found"
                          : "Bad Request";

    QString headers =
        QString("HTTP/1.1 %1 %2\r\n"
                "Content-Type: %3\r\n"
                "Content-Length: %4\r\n"
                "Cache-Control: max-age=60\r\n"
                "ETag: \"%5\"\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "\r\n")
            .arg(resp.status)
            .arg(QString(statusText))
            .arg(resp.contentType)
            .arg(resp.body.size())
            .arg(resp.etag);

    socket->write(headers.toUtf8());
    if (resp.status != 304) socket->write(resp.body);
    socket->flush();
}
```

---

## Routing

```cpp
RssServer::Response RssServer::dispatch(const QString& method,
                                         const QString& path,
                                         const QMap<QString,QString>& headers) {
    if (method != "GET" && method != "POST") return { 405, "text/plain", "Method Not Allowed" };

    if (path == "/feed.xml")     return serveAggregatedFeed(headers);
    if (path == "/my/feed.xml")  return serveMyFeed(headers);
    if (path == "/opml")         return (method == "POST") ? importOpml(headers) : exportOpml();
    if (path == "/health")       return { 200, "text/plain", "OK" };
    if (path == "/")             return serveIndex();

    // /@<pubkey>/feed.xml
    static const QRegularExpression authorPath(R"(^/@([0-9a-f]{64})/feed\.xml$)");
    QRegularExpressionMatch m = authorPath.match(path);
    if (m.hasMatch()) return serveAuthorFeed(m.captured(1), headers);

    return { 404, "text/plain", "Not Found" };
}
```

---

## Cache Headers and ETag Support

Every feed response includes:
- `Cache-Control: max-age=60` — clients may cache for 60 seconds
- `ETag: "<md5-of-body>"` — strong ETag based on MD5 of the response body

### ETag Generation

```cpp
QString RssServer::etagFor(const QByteArray& body) {
    return QCryptographicHash::hash(body, QCryptographicHash::Md5).toHex();
}
```

### Conditional GET (`If-None-Match`)

```cpp
RssServer::Response RssServer::serveAggregatedFeed(const QMap<QString,QString>& headers) {
    QJsonArray posts = m_feed->getAggregatedFeed();
    QByteArray body  = buildAtomFeed("aggregated", "My Subscriptions",
                                      "/feed.xml", posts);
    QString etag = etagFor(body);

    if (headers.value("if-none-match") == QString("\"%1\"").arg(etag)) {
        return { 304, {}, {}, etag };  // Not Modified
    }
    return { 200, "application/atom+xml; charset=utf-8", body, etag };
}
```

---

## Atom 1.0 XML Generation

### Feed Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Martin's Blog</title>
  <subtitle>Building things at Logos</subtitle>
  <id>urn:logos-blog:a3f8e1d2c4b5...</id>
  <link href="http://localhost:8484/@a3f8e1d2c4b5.../feed.xml" rel="self"
        type="application/atom+xml"/>
  <updated>2026-03-14T06:46:00Z</updated>
  <generator uri="https://github.com/logos-co/logos-blog" version="0.1.0">
    Logos Blog
  </generator>
  <author>
    <name>Martin</name>
  </author>

  <entry>
    <title>My First Post</title>
    <id>urn:logos-blog:a3f8e1d2c4b5...:550e8400-e29b-41d4-a716-446655440000</id>
    <published>2026-03-14T06:46:00Z</published>
    <updated>2026-03-14T06:46:00Z</updated>
    <author>
      <name>Martin</name>
    </author>
    <summary>A short introduction</summary>
    <content type="html"><![CDATA[<p>Rendered HTML from markdown</p>]]></content>
    <category term="logos"/>
    <category term="p2p"/>
    <link href="http://localhost:8484/@a3f8e1d2c4b5.../feed.xml#550e8400..." rel="alternate"/>
  </entry>
</feed>
```

### C++ Builder

```cpp
QByteArray RssServer::buildAtomFeed(const QString& feedId,
                                     const QString& title,
                                     const QString& selfUrl,
                                     const QJsonArray& posts,
                                     const QString& authorName) {
    QString base = QString("http://localhost:%1").arg(m_port);
    QString updated = posts.isEmpty() ? isoNow()
                    : posts[0].toObject()["created_at"].toString();

    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n";
    xml += QString("  <title>%1</title>\n").arg(xmlEscape(title));
    xml += QString("  <id>urn:logos-blog:%1</id>\n").arg(feedId);
    xml += QString("  <link href=\"%1%2\" rel=\"self\" type=\"application/atom+xml\"/>\n")
               .arg(base, selfUrl);
    xml += QString("  <updated>%1</updated>\n").arg(updated);
    xml += "  <generator version=\"0.1.0\">Logos Blog</generator>\n";
    if (!authorName.isEmpty())
        xml += QString("  <author><name>%1</name></author>\n").arg(xmlEscape(authorName));

    for (const QJsonValue& v : posts) {
        QJsonObject post = v.toObject();
        QString authorPubkey = post["author_pubkey"].toString();
        QString aName        = post["author_name"].toString();
        xml += postToAtomEntry(post, authorPubkey, aName, base);
    }

    xml += "</feed>\n";
    return xml.toUtf8();
}

QString RssServer::postToAtomEntry(const QJsonObject& post,
                                    const QString& authorPubkey,
                                    const QString& authorName,
                                    const QString& base) {
    QString postId = post["id"].toString();
    QString html   = markdownToHtml(post["body"].toString());

    QString entry;
    entry += "  <entry>\n";
    entry += QString("    <title>%1</title>\n").arg(xmlEscape(post["title"].toString()));
    entry += QString("    <id>urn:logos-blog:%1:%2</id>\n").arg(authorPubkey, postId);
    entry += QString("    <published>%1</published>\n").arg(post["created_at"].toString());
    entry += QString("    <updated>%1</updated>\n").arg(post["updated_at"].toString());
    if (!authorName.isEmpty())
        entry += QString("    <author><name>%1</name></author>\n").arg(xmlEscape(authorName));
    if (!post["summary"].toString().isEmpty())
        entry += QString("    <summary>%1</summary>\n").arg(xmlEscape(post["summary"].toString()));
    entry += QString("    <content type=\"html\"><![CDATA[%1]]></content>\n").arg(html);
    for (const QJsonValue& tag : post["tags"].toArray())
        entry += QString("    <category term=\"%1\"/>\n").arg(xmlEscape(tag.toString()));
    entry += QString("    <link href=\"%1/@%2/feed.xml#%3\" rel=\"alternate\"/>\n")
                 .arg(base, authorPubkey, postId);
    entry += "  </entry>\n";
    return entry;
}
```

### Markdown to HTML

MVP: minimal escaping. Replace `# Heading` → `<h1>`, `**bold**` → `<b>`, etc. via regex. Full CommonMark parsing is Phase 6.

```cpp
QString RssServer::markdownToHtml(const QString& md) {
    QString html = md.toHtmlEscaped();
    // Headings
    html.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption),
                 "<h3>\\1</h3>");
    html.replace(QRegularExpression("^## (.+)$",  QRegularExpression::MultilineOption),
                 "<h2>\\1</h2>");
    html.replace(QRegularExpression("^# (.+)$",   QRegularExpression::MultilineOption),
                 "<h1>\\1</h1>");
    // Inline: bold, italic, code
    html.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");
    html.replace(QRegularExpression("\\*(.+?)\\*"),       "<em>\\1</em>");
    html.replace(QRegularExpression("`(.+?)`"),            "<code>\\1</code>");
    // Paragraphs: double newline
    html.replace(QRegularExpression("\\n\\n"), "</p><p>");
    return "<p>" + html + "</p>";
}
```

---

## OPML Export

OPML 2.0 format. Exported list of subscriptions for use in any RSS reader.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<opml version="2.0">
  <head>
    <title>Logos Blog Subscriptions</title>
    <dateCreated>Sat, 14 Mar 2026 06:46:00 GMT</dateCreated>
  </head>
  <body>
    <outline text="Martin K" type="rss"
             xmlUrl="http://localhost:8484/@a3f8e1d2.../feed.xml"
             htmlUrl="http://localhost:8484/@a3f8e1d2..."
             description="pubkey: a3f8e1d2..."/>
  </body>
</opml>
```

### Export Handler

```cpp
RssServer::Response RssServer::exportOpml() {
    QJsonArray subs = m_feed->listSubscriptions();
    QString base = QString("http://localhost:%1").arg(m_port);

    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<opml version=\"2.0\">\n  <head>\n";
    xml += QString("    <title>Logos Blog Subscriptions</title>\n");
    xml += QString("    <dateCreated>%1</dateCreated>\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date));
    xml += "  </head>\n  <body>\n";

    for (const QJsonValue& v : subs) {
        QJsonObject sub = v.toObject();
        QString pubkey = sub["pubkey"].toString();
        QString name   = xmlEscape(sub["name"].toString());
        xml += QString("    <outline text=\"%1\" type=\"rss\"\n"
                       "             xmlUrl=\"%2/@%3/feed.xml\"\n"
                       "             htmlUrl=\"%2/@%3\"\n"
                       "             description=\"pubkey: %3\"/>\n")
                   .arg(name, base, pubkey);
    }

    xml += "  </body>\n</opml>\n";
    return { 200, "text/x-opml; charset=utf-8", xml.toUtf8(), etagFor(xml.toUtf8()) };
}
```

## OPML Import

Parse incoming OPML XML and subscribe to each `<outline type="rss">` entry.

```cpp
RssServer::Response RssServer::importOpml(const QMap<QString,QString>& headers) {
    // Body read from socket before dispatch — stored in m_pendingBody
    QDomDocument doc;
    if (!doc.setContent(m_pendingBody)) return { 400, "text/plain", "Invalid XML" };

    QDomNodeList outlines = doc.elementsByTagName("outline");
    int imported = 0;
    for (int i = 0; i < outlines.size(); ++i) {
        QDomElement el = outlines.at(i).toElement();
        QString xmlUrl = el.attribute("xmlUrl");
        QString name   = el.attribute("text");

        // Extract pubkey from URL: http://localhost:8484/@<pubkey>/feed.xml
        static const QRegularExpression re(R"(/@([0-9a-f]{64})/feed\.xml)");
        QRegularExpressionMatch m = re.match(xmlUrl);
        if (!m.hasMatch()) continue;

        QString pubkey = m.captured(1);
        m_feed->subscribe(pubkey, name.isEmpty() ? pubkey.left(8) : name);
        ++imported;
    }

    QByteArray body = QString("{\"imported\": %1}").arg(imported).toUtf8();
    return { 200, "application/json", body };
}
```

---

## Bind Address Configuration

The server defaults to `127.0.0.1` (loopback only). Users can change it to `0.0.0.0` to expose on all interfaces (LAN/public), or a specific IP.

```cpp
bool RssServer::start(const QString& bindAddress, int port) {
    m_bind = bindAddress;
    m_port = port;

    if (!m_server) m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &RssServer::onNewConnection);

    QHostAddress addr;
    if (!addr.setAddress(bindAddress)) {
        addr = QHostAddress::LocalHost;
    }

    if (!m_server->listen(addr, port)) {
        qWarning() << "RssServer: failed to listen on" << bindAddress << port
                   << m_server->errorString();
        return false;
    }

    emit started(port);
    return true;
}
```

Settings stored in kv_module:
- `blog:settings:rss_port` → `"8484"`
- `blog:settings:rss_bind` → `"127.0.0.1"`

Read on `BlogPlugin::initLogos`, applied when starting `RssServer`.
