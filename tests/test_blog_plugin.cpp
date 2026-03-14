// tests/test_blog_plugin.cpp
// Tests for BlogPlugin envelope building/signing and search logic.
//
// Rather than compiling the full BlogPlugin (which pulls in the Logos SDK
// plugin infrastructure), these tests exercise the two pure-logic pieces
// directly:
//
//  1. buildSignedEnvelope  — mirrors BlogPlugin::buildSignedEnvelope exactly.
//  2. postMatchesQuery     — mirrors the private helper in blog_plugin.cpp.
//
// Both functions are reproduced inline so the tests run with zero external
// dependencies beyond Qt6::Core and OpenSSL.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>

#include "crypto.h"

// ── Helpers (mirrors BlogPlugin private logic) ────────────────────────────────

// Mirrors BlogPlugin::buildSignedEnvelope
static QString buildSignedEnvelope(const QString& pubkeyHex,
                                    const QString& privkeyHex,
                                    const QString& displayName,
                                    const QString& type,
                                    const QJsonObject& typePayload)
{
    if (pubkeyHex.isEmpty() || privkeyHex.isEmpty()) return {};

    QJsonObject author;
    author["pubkey"] = pubkeyHex;
    author["name"]   = displayName;

    QJsonObject envelope;
    envelope["version"]   = 1;
    envelope["type"]      = type;
    envelope["author"]    = author;
    envelope["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    for (auto it = typePayload.constBegin(); it != typePayload.constEnd(); ++it)
        envelope[it.key()] = it.value();

    // Sign the envelope WITHOUT the signature field
    const QByteArray canonical = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const QString sig = Crypto::sign(privkeyHex, canonical);
    if (sig.isEmpty()) return {};

    envelope["signature"] = sig;
    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

// Mirrors the static postMatchesQuery() in blog_plugin.cpp
static bool postMatchesQuery(const QJsonObject& post, const QString& query)
{
    const QString q = query.toLower();
    if (post["title"].toString().toLower().contains(q))   return true;
    if (post["body"].toString().toLower().contains(q))    return true;
    if (post["summary"].toString().toLower().contains(q)) return true;
    for (const auto& tag : post["tags"].toArray()) {
        if (tag.toString().toLower().contains(q)) return true;
    }
    return false;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestBlogPlugin : public QObject {
    Q_OBJECT

private slots:
    // buildSignedEnvelope
    void buildEnvelope_allRequiredFields();
    void buildEnvelope_signaturePresent();
    void buildEnvelope_signatureVerifiable();
    void buildEnvelope_canonicalJsonUsedForSigning();
    void buildEnvelope_emptyKeysReturnEmpty();

    // searchPosts / postMatchesQuery
    void search_matchesTitle();
    void search_matchesBody();
    void search_matchesTags();
    void search_caseInsensitive();
    void search_noResultsForNonMatch();
    void search_matchesSummary();
};

// ── buildSignedEnvelope ───────────────────────────────────────────────────────

void TestBlogPlugin::buildEnvelope_allRequiredFields()
{
    const Keypair kp = Crypto::generateEd25519Keypair();

    QJsonObject payload;
    QJsonObject post;
    post["id"]    = "test-id";
    post["title"] = "Hello";
    payload["post"] = post;

    const QString json = buildSignedEnvelope(kp.pubkeyHex, kp.privkeyHex,
                                              "Alice", "post", payload);
    QVERIFY(!json.isEmpty());

    const QJsonObject env = QJsonDocument::fromJson(json.toUtf8()).object();
    QVERIFY(env.contains("version"));
    QVERIFY(env.contains("type"));
    QVERIFY(env.contains("author"));
    QVERIFY(env.contains("timestamp"));
    QVERIFY(env.contains("signature"));
    QVERIFY(env.contains("post"));

    QCOMPARE(env["version"].toInt(), 1);
    QCOMPARE(env["type"].toString(), QString("post"));
    QCOMPARE(env["author"].toObject()["pubkey"].toString(), kp.pubkeyHex);
    QCOMPARE(env["author"].toObject()["name"].toString(), QString("Alice"));
}

void TestBlogPlugin::buildEnvelope_signaturePresent()
{
    const Keypair kp = Crypto::generateEd25519Keypair();
    const QString json = buildSignedEnvelope(kp.pubkeyHex, kp.privkeyHex,
                                              "Bob", "post", {});
    QVERIFY(!json.isEmpty());

    const QJsonObject env = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString sig = env["signature"].toString();
    QVERIFY(!sig.isEmpty());
    QCOMPARE(sig.length(), 128);  // 64-byte Ed25519 sig → 128 hex chars

    // Must be valid hex
    QVERIFY(QRegularExpression("^[0-9a-f]{128}$").match(sig).hasMatch());
}

void TestBlogPlugin::buildEnvelope_signatureVerifiable()
{
    const Keypair kp = Crypto::generateEd25519Keypair();

    QJsonObject payload;
    payload["delete"] = QJsonObject{{"post_id", "xyz"}};

    const QString json = buildSignedEnvelope(kp.pubkeyHex, kp.privkeyHex,
                                              "Carol", "delete", payload);
    QVERIFY(!json.isEmpty());

    const QJsonObject env = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString sig = env["signature"].toString();

    // Reconstruct the canonical bytes (envelope without "signature")
    QJsonObject toVerify = env;
    toVerify.remove("signature");
    const QByteArray canonical = QJsonDocument(toVerify).toJson(QJsonDocument::Compact);

    QVERIFY(Crypto::verify(kp.pubkeyHex, sig, canonical));
}

void TestBlogPlugin::buildEnvelope_canonicalJsonUsedForSigning()
{
    // Verify that if we tamper with ANY field after signing, verification fails.
    const Keypair kp = Crypto::generateEd25519Keypair();
    const QString json = buildSignedEnvelope(kp.pubkeyHex, kp.privkeyHex,
                                              "Dave", "post", {});
    QVERIFY(!json.isEmpty());

    QJsonObject env = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString sig = env["signature"].toString();

    // Tamper: change the type field
    env["type"] = "evil";
    env.remove("signature");
    const QByteArray tampered = QJsonDocument(env).toJson(QJsonDocument::Compact);

    // Signature must no longer verify against the tampered bytes
    QVERIFY(!Crypto::verify(kp.pubkeyHex, sig, tampered));
}

void TestBlogPlugin::buildEnvelope_emptyKeysReturnEmpty()
{
    // Guard: empty pubkey or privkey should produce no envelope
    QVERIFY(buildSignedEnvelope("", "privkey", "name", "post", {}).isEmpty());
    QVERIFY(buildSignedEnvelope("pubkey", "", "name", "post", {}).isEmpty());
}

// ── postMatchesQuery ──────────────────────────────────────────────────────────

static QJsonObject makePost(const QString& title, const QString& body,
                             const QString& summary, const QStringList& tags)
{
    QJsonObject post;
    post["title"]   = title;
    post["body"]    = body;
    post["summary"] = summary;
    QJsonArray arr;
    for (const auto& t : tags) arr.append(t);
    post["tags"] = arr;
    return post;
}

void TestBlogPlugin::search_matchesTitle()
{
    const QJsonObject post = makePost("Hello World", "body", "sum", {});
    QVERIFY( postMatchesQuery(post, "hello"));
    QVERIFY(!postMatchesQuery(post, "goodbye"));
}

void TestBlogPlugin::search_matchesBody()
{
    const QJsonObject post = makePost("title", "This is the body content", "sum", {});
    QVERIFY( postMatchesQuery(post, "body content"));
    QVERIFY(!postMatchesQuery(post, "not in body"));
}

void TestBlogPlugin::search_matchesTags()
{
    const QJsonObject post = makePost("title", "body", "sum", {"rust", "qt"});
    QVERIFY( postMatchesQuery(post, "rust"));
    QVERIFY( postMatchesQuery(post, "qt"));
    QVERIFY(!postMatchesQuery(post, "python"));
}

void TestBlogPlugin::search_caseInsensitive()
{
    const QJsonObject post = makePost("Hello World", "BODY TEXT", "sum", {"TagName"});
    QVERIFY(postMatchesQuery(post, "HELLO"));
    QVERIFY(postMatchesQuery(post, "hello"));
    QVERIFY(postMatchesQuery(post, "body text"));
    QVERIFY(postMatchesQuery(post, "BODY TEXT"));
    QVERIFY(postMatchesQuery(post, "tagname"));
    QVERIFY(postMatchesQuery(post, "TAGNAME"));
}

void TestBlogPlugin::search_noResultsForNonMatch()
{
    const QJsonObject post = makePost("Hello", "World", "summary", {"tag1"});
    QVERIFY(!postMatchesQuery(post, "nonexistent"));
    QVERIFY(!postMatchesQuery(post, "xyz"));
}

void TestBlogPlugin::search_matchesSummary()
{
    const QJsonObject post = makePost("title", "body", "executive summary here", {});
    QVERIFY( postMatchesQuery(post, "executive"));
    QVERIFY(!postMatchesQuery(post, "abstract"));
}

QTEST_GUILESS_MAIN(TestBlogPlugin)
#include "test_blog_plugin.moc"
