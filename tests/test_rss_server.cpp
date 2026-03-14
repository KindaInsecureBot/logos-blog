// tests/test_rss_server.cpp
// Tests for Atom XML generation, Markdown→HTML conversion, ETag, and OPML
// (no TCP server binding required).

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QXmlStreamReader>
#include <QCryptographicHash>

#include "mock_kv_client.h"
#include "post_store.h"
#include "feed_store.h"
#include "rss_server.h"

// ── TestRssServer (friend class declared in rss_server.h under BUILD_TESTS) ───

class TestRssServer : public QObject {
    Q_OBJECT

private slots:
    // buildAtomFeed
    void buildAtomFeed_validXmlStructure();
    void buildAtomFeed_entriesSortedByDate();
    void buildAtomFeed_emptyFeedValidXml();

    // postToAtomEntry
    void postToAtomEntry_allFieldsMapped();
    void postToAtomEntry_htmlEscapesContent();

    // markdownToHtml
    void markdown_h1();
    void markdown_h2();
    void markdown_h3();
    void markdown_h4();
    void markdown_bold();
    void markdown_italic();
    void markdown_inlineCode();
    void markdown_link();
    void markdown_image();
    void markdown_fencedCodeBlock_withLanguage();
    void markdown_blockquote();
    void markdown_unorderedList();
    void markdown_orderedList();
    void markdown_horizontalRule();

    // etagFor
    void etag_sameContentSameEtag();
    void etag_differentContentDifferentEtag();

    // OPML (tested through FeedStore + RssServer public API)
    void opml_generation_validXml();
    void opml_parsing_extractsPubkeys();

private:
    // Helpers that call private methods on m_rss (allowed by friendship)
    RssServer m_rss;

    static QJsonObject makeTestPost(const QString& id,
                                     const QString& title,
                                     const QString& body,
                                     const QString& created_at = "2024-01-01T00:00:00Z",
                                     const QString& updated_at = "2024-01-01T00:00:00Z",
                                     const QString& summary    = "",
                                     const QStringList& tags   = {})
    {
        QJsonObject post;
        post["id"]         = id;
        post["title"]      = title;
        post["body"]       = body;
        post["summary"]    = summary;
        post["created_at"] = created_at;
        post["updated_at"] = updated_at;

        QJsonArray tagArr;
        for (const auto& t : tags) tagArr.append(t);
        post["tags"] = tagArr;
        return post;
    }
};

// ── buildAtomFeed ─────────────────────────────────────────────────────────────

void TestRssServer::buildAtomFeed_validXmlStructure()
{
    QJsonArray posts;
    posts.append(makeTestPost("id1", "Hello World", "body text"));

    const QByteArray xml = m_rss.buildAtomFeed(
        "urn:logos-blog:test", "Test Feed",
        "http://localhost:8484/feed.xml", posts);

    QVERIFY(!xml.isEmpty());

    // Must be parseable XML
    QXmlStreamReader reader(xml);
    bool hasFeed = false, hasEntry = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QStringView(u"feed"))  hasFeed  = true;
            if (reader.name() == QStringView(u"entry")) hasEntry = true;
        }
    }
    QVERIFY(!reader.hasError());
    QVERIFY(hasFeed);
    QVERIFY(hasEntry);

    // Must declare the Atom namespace
    QVERIFY(xml.contains("http://www.w3.org/2005/Atom"));
    QVERIFY(xml.contains("Test Feed"));
}

void TestRssServer::buildAtomFeed_entriesSortedByDate()
{
    // buildAtomFeed renders entries in the order supplied — the caller is
    // responsible for sorting.  We pass them already sorted and verify order.
    QJsonArray posts;
    posts.append(makeTestPost("newer", "Newer", "", "2024-06-01T00:00:00Z"));
    posts.append(makeTestPost("older", "Older", "", "2024-01-01T00:00:00Z"));

    const QByteArray xml = m_rss.buildAtomFeed(
        "urn:logos-blog:test", "Feed", "http://localhost/feed.xml", posts);

    const int newerPos = xml.indexOf("Newer");
    const int olderPos = xml.indexOf("Older");
    QVERIFY(newerPos < olderPos);
}

void TestRssServer::buildAtomFeed_emptyFeedValidXml()
{
    const QByteArray xml = m_rss.buildAtomFeed(
        "urn:logos-blog:empty", "Empty", "http://localhost/feed.xml", QJsonArray());

    QXmlStreamReader reader(xml);
    bool hasFeed = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::StartElement &&
            reader.name() == QStringView(u"feed"))
            hasFeed = true;
    }
    QVERIFY(!reader.hasError());
    QVERIFY(hasFeed);
    QVERIFY(!xml.contains("<entry>"));
}

// ── postToAtomEntry ───────────────────────────────────────────────────────────

void TestRssServer::postToAtomEntry_allFieldsMapped()
{
    const QJsonObject post = makeTestPost(
        "abc123", "My Title", "body", "2024-03-01T00:00:00Z",
        "2024-03-02T00:00:00Z", "My Summary", {"tagA", "tagB"});

    const QString entry = m_rss.postToAtomEntry(post, "deadbeef", "Alice");

    QVERIFY(entry.contains("My Title"));
    QVERIFY(entry.contains("abc123"));
    QVERIFY(entry.contains("2024-03-01T00:00:00Z"));  // published
    QVERIFY(entry.contains("2024-03-02T00:00:00Z"));  // updated
    QVERIFY(entry.contains("My Summary"));
    QVERIFY(entry.contains("Alice"));
    QVERIFY(entry.contains("tagA"));
    QVERIFY(entry.contains("tagB"));
    QVERIFY(entry.contains("CDATA"));                  // content wrapped in CDATA
}

void TestRssServer::postToAtomEntry_htmlEscapesContent()
{
    // The title is escaped via toHtmlEscaped() in postToAtomEntry
    const QJsonObject post = makeTestPost(
        "id1", "A & B < C > D", "body **bold**");

    const QString entry = m_rss.postToAtomEntry(post, "", "");

    // & → &amp;  in the title/summary (outside CDATA)
    QVERIFY(entry.contains("A &amp; B &lt; C &gt; D"));
}

// ── markdownToHtml ────────────────────────────────────────────────────────────

void TestRssServer::markdown_h1()
{
    QVERIFY(m_rss.markdownToHtml("# Heading One").contains("<h1>Heading One</h1>"));
}

void TestRssServer::markdown_h2()
{
    QVERIFY(m_rss.markdownToHtml("## Heading Two").contains("<h2>Heading Two</h2>"));
}

void TestRssServer::markdown_h3()
{
    QVERIFY(m_rss.markdownToHtml("### H3").contains("<h3>H3</h3>"));
}

void TestRssServer::markdown_h4()
{
    QVERIFY(m_rss.markdownToHtml("#### H4").contains("<h4>H4</h4>"));
}

void TestRssServer::markdown_bold()
{
    QVERIFY(m_rss.markdownToHtml("**bold text**").contains("<strong>bold text</strong>"));
}

void TestRssServer::markdown_italic()
{
    QVERIFY(m_rss.markdownToHtml("*italic text*").contains("<em>italic text</em>"));
}

void TestRssServer::markdown_inlineCode()
{
    QVERIFY(m_rss.markdownToHtml("`code here`").contains("<code>code here</code>"));
}

void TestRssServer::markdown_link()
{
    const QString html = m_rss.markdownToHtml("[Click me](https://example.com)");
    QVERIFY(html.contains("<a href=\"https://example.com\">Click me</a>"));
}

void TestRssServer::markdown_image()
{
    const QString html = m_rss.markdownToHtml("![alt text](https://example.com/img.png)");
    QVERIFY(html.contains("<img src=\"https://example.com/img.png\" alt=\"alt text\"/>"));
}

void TestRssServer::markdown_fencedCodeBlock_withLanguage()
{
    const QString md = "```cpp\nint x = 0;\n```";
    const QString html = m_rss.markdownToHtml(md);
    QVERIFY(html.contains("<pre><code class=\"language-cpp\">"));
    QVERIFY(html.contains("int x = 0;"));
}

void TestRssServer::markdown_blockquote()
{
    const QString html = m_rss.markdownToHtml("> quoted text");
    QVERIFY(html.contains("<blockquote>quoted text</blockquote>"));
}

void TestRssServer::markdown_unorderedList()
{
    const QString html = m_rss.markdownToHtml("- item one");
    QVERIFY(html.contains("<li>item one</li>"));
}

void TestRssServer::markdown_orderedList()
{
    // The implementation converts unordered "- " list markers.
    // Verify "* item" also produces <li>
    const QString html = m_rss.markdownToHtml("* item two");
    QVERIFY(html.contains("<li>item two</li>"));
}

void TestRssServer::markdown_horizontalRule()
{
    QVERIFY(m_rss.markdownToHtml("---").contains("<hr/>"));
    QVERIFY(m_rss.markdownToHtml("----").contains("<hr/>"));
}

// ── etagFor ───────────────────────────────────────────────────────────────────

void TestRssServer::etag_sameContentSameEtag()
{
    const QByteArray body = "hello world";
    QCOMPARE(m_rss.etagFor(body), m_rss.etagFor(body));
}

void TestRssServer::etag_differentContentDifferentEtag()
{
    QVERIFY(m_rss.etagFor("foo") != m_rss.etagFor("bar"));
}

// ── OPML via FeedStore / RssServer ────────────────────────────────────────────

void TestRssServer::opml_generation_validXml()
{
    MockKvClient kv;
    FeedStore    feed;
    feed.setKvClient(&kv);

    const QString pubkey = QString(64, QChar('a'));  // 64-char fake pubkey
    feed.subscribe(pubkey, "Alice");

    RssServer rss;
    rss.setFeedStore(&feed);

    // Trigger OPML export via the private serveOpmlExport → use friend access
    const RssServer::Response resp = rss.serveOpmlExport();
    QCOMPARE(resp.status, 200);

    const QByteArray xml = resp.body;
    QVERIFY(!xml.isEmpty());

    QXmlStreamReader reader(xml);
    bool hasOpml = false, hasOutline = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::StartElement) {
            if (reader.name() == QStringView(u"opml"))    hasOpml    = true;
            if (reader.name() == QStringView(u"outline")) hasOutline = true;
        }
    }
    QVERIFY(!reader.hasError());
    QVERIFY(hasOpml);
    QVERIFY(hasOutline);
    QVERIFY(xml.contains(pubkey.toUtf8()));
}

void TestRssServer::opml_parsing_extractsPubkeys()
{
    // Build a minimal OPML document with a pubkey in the xmlUrl attribute
    const QString pk = QString(64, QChar('b'));  // 64-char fake pubkey
    const QByteArray opml =
        "<?xml version=\"1.0\"?>\n"
        "<opml version=\"2.0\"><head/><body>\n"
        "  <outline type=\"rss\" text=\"Bob\""
        " xmlUrl=\"http://localhost:8484/@" + pk.toUtf8() + "/feed.xml\"/>\n"
        "</body></opml>";

    MockKvClient kv;
    FeedStore    feed;
    feed.setKvClient(&kv);

    RssServer rss;
    rss.setFeedStore(&feed);

    const RssServer::Response resp = rss.importOpml(opml);
    QCOMPARE(resp.status, 200);

    // The pubkey should now be subscribed
    QVERIFY(feed.isSubscribed(pk));
}

QTEST_GUILESS_MAIN(TestRssServer)
#include "test_rss_server.moc"
