// tests/test_feed_store.cpp
// Unit tests for FeedStore ingestion, query, rate limiting, and caps.
// Uses real Ed25519 crypto (via src/crypto.cpp) to build valid signed envelopes,
// and an in-memory MockKvClient for storage.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>

#include "mock_kv_client.h"
#include "feed_store.h"
#include "crypto.h"

// ── Envelope builder ──────────────────────────────────────────────────────────
// Mirrors the logic in BlogPlugin::buildSignedEnvelope and FeedStore::verifyEnvelopeSignature.

static QJsonObject buildEnvelope(const Keypair& kp, const QString& type,
                                  const QJsonObject& payload,
                                  const QString& authorName = "Test Author")
{
    QJsonObject author;
    author["pubkey"] = kp.pubkeyHex;
    author["name"]   = authorName;

    QJsonObject envelope;
    envelope["version"]   = 1;
    envelope["type"]      = type;
    envelope["author"]    = author;
    envelope["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it)
        envelope[it.key()] = it.value();

    // Sign: canonical = compact JSON without "signature" key
    const QByteArray canonical = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const QString sig = Crypto::sign(kp.privkeyHex, canonical);
    envelope["signature"] = sig;
    return envelope;
}

static QJsonObject makePost(const QString& postId,
                             const QString& updatedAt = "2024-01-01T00:00:00Z")
{
    QJsonObject post;
    post["id"]         = postId;
    post["title"]      = "Post " + postId.left(8);
    post["body"]       = "body text";
    post["summary"]    = "summary";
    post["tags"]       = QJsonArray();
    post["created_at"] = "2024-01-01T00:00:00Z";
    post["updated_at"] = updatedAt;
    post["published"]  = true;
    return post;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestFeedStore : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Basic ingestion
    void ingestPost_validEnvelope_stored();
    void ingestPost_keyFormatCorrect();

    // Signature verification
    void ingestPost_rejectsBadSignature();
    void ingestPost_rejectsNoSignature();

    // Subscription gate
    void ingestPost_rejectsUnsubscribedAuthor();

    // LWW conflict resolution
    void ingestPost_deduplication_newerWins();
    void ingestPost_deduplication_olderRejected();
    void ingestPost_deduplication_sameTimestampLargerSigWins();

    // Limits
    void ingestPost_rejectsOversizedBody();
    void ingestPost_rateLimiting_exceededRejected();
    void ingestPost_perAuthorCap_exceededRejected();

    // ingestDelete
    void ingestDelete_removesPost();
    void ingestDelete_rejectsBadSignature();

    // ingestProfile
    void ingestProfile_storesProfile();
    void ingestProfile_updatesSubscriptionName();
    void ingestProfile_rejectsBadSignature();

    // Queries
    void getAggregatedFeed_mergesAuthors_sortedByDate();
    void getPostsByTag_filtersCorrectly();

private:
    MockKvClient* m_kv   = nullptr;
    FeedStore*    m_feed = nullptr;

    // Subscribe a keypair's author and return the keypair
    Keypair subscribeAuthor(const QString& name = "Author")
    {
        const Keypair kp = Crypto::generateEd25519Keypair();
        m_feed->subscribe(kp.pubkeyHex, name);
        return kp;
    }
};

void TestFeedStore::init()
{
    m_kv   = new MockKvClient();
    m_feed = new FeedStore();
    m_feed->setKvClient(m_kv);
}

void TestFeedStore::cleanup()
{
    delete m_feed;
    delete m_kv;
    m_feed = nullptr;
    m_kv   = nullptr;
}

// ── Basic ingestion ───────────────────────────────────────────────────────────

void TestFeedStore::ingestPost_validEnvelope_stored()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject post = makePost(postId);
    const QJsonObject env  = buildEnvelope(kp, "post", {{"post", post}});

    QVERIFY(m_feed->ingestPost(env));

    // Verify retrievable via getPost
    const QJsonObject stored = m_feed->getPost(kp.pubkeyHex, postId);
    QVERIFY(!stored.isEmpty());
    QCOMPARE(stored["id"].toString(), postId);
    QVERIFY(stored["verified"].toBool());
}

void TestFeedStore::ingestPost_keyFormatCorrect()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(postId)}});
    QVERIFY(m_feed->ingestPost(env));

    const QString expectedKey = "feed:" + kp.pubkeyHex + ":" + postId;
    QVERIFY(m_kv->m_store.contains(expectedKey));
}

// ── Signature verification ────────────────────────────────────────────────────

void TestFeedStore::ingestPost_rejectsBadSignature()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(postId)}});

    // Tamper with the post title — signature now invalid
    QJsonObject post = env["post"].toObject();
    post["title"] = "tampered";
    env["post"]   = post;

    QVERIFY(!m_feed->ingestPost(env));
}

void TestFeedStore::ingestPost_rejectsNoSignature()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(postId)}});
    env.remove("signature");
    QVERIFY(!m_feed->ingestPost(env));
}

// ── Subscription gate ─────────────────────────────────────────────────────────

void TestFeedStore::ingestPost_rejectsUnsubscribedAuthor()
{
    const Keypair kp = Crypto::generateEd25519Keypair();  // NOT subscribed
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(postId)}});
    QVERIFY(!m_feed->ingestPost(env));
}

// ── LWW conflict resolution ───────────────────────────────────────────────────

void TestFeedStore::ingestPost_deduplication_newerWins()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Ingest older version
    QJsonObject env1 = buildEnvelope(kp, "post",
        {{"post", makePost(postId, "2024-01-01T00:00:00Z")}});
    QVERIFY(m_feed->ingestPost(env1));

    // Ingest newer version — should succeed
    QJsonObject env2 = buildEnvelope(kp, "post",
        {{"post", makePost(postId, "2024-06-01T00:00:00Z")}});
    QVERIFY(m_feed->ingestPost(env2));

    // Stored version should reflect the newer updated_at
    const QJsonObject stored = m_feed->getPost(kp.pubkeyHex, postId);
    QCOMPARE(stored["updated_at"].toString(), QString("2024-06-01T00:00:00Z"));
}

void TestFeedStore::ingestPost_deduplication_olderRejected()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Ingest newer first
    QJsonObject env1 = buildEnvelope(kp, "post",
        {{"post", makePost(postId, "2024-06-01T00:00:00Z")}});
    QVERIFY(m_feed->ingestPost(env1));

    // Try to ingest older — should be rejected
    QJsonObject env2 = buildEnvelope(kp, "post",
        {{"post", makePost(postId, "2024-01-01T00:00:00Z")}});
    QVERIFY(!m_feed->ingestPost(env2));

    // Stored version must not have been downgraded
    const QJsonObject stored = m_feed->getPost(kp.pubkeyHex, postId);
    QCOMPARE(stored["updated_at"].toString(), QString("2024-06-01T00:00:00Z"));
}

void TestFeedStore::ingestPost_deduplication_sameTimestampLargerSigWins()
{
    // Same updated_at tie-break: lexicographically larger signature wins.
    // We generate two envelopes for the same post ID and same updated_at,
    // then verify that only one survives deterministically.
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString ts = "2024-01-01T12:00:00Z";

    const QJsonObject env1 = buildEnvelope(kp, "post", {{"post", makePost(postId, ts)}});
    const QJsonObject env2 = buildEnvelope(kp, "post", {{"post", makePost(postId, ts)}});

    // env1 and env2 have the same timestamp but different signatures (different timestamps in header).
    // Ingest env1, then env2: exactly one stored version (the one with the larger sig).
    QVERIFY(m_feed->ingestPost(env1));

    const bool env2Accepted = m_feed->ingestPost(env2);
    // Whether env2 wins or loses, the stored post must have exactly one entry
    const QJsonObject stored = m_feed->getPost(kp.pubkeyHex, postId);
    QVERIFY(!stored.isEmpty());

    // The stored signature must equal whichever envelope won
    const QString storedSig = stored["signature"].toString();
    const QString sig1 = env1["signature"].toString();
    const QString sig2 = env2["signature"].toString();
    if (env2Accepted) {
        QCOMPARE(storedSig, sig2);
    } else {
        QCOMPARE(storedSig, sig1);
    }
}

// ── Limits ────────────────────────────────────────────────────────────────────

void TestFeedStore::ingestPost_rejectsOversizedBody()
{
    const Keypair kp = subscribeAuthor();
    QJsonObject post = makePost(QUuid::createUuid().toString(QUuid::WithoutBraces));
    // 513 KB body — exceeds kMaxPostBodyBytes (512 KB)
    post["body"] = QString(513 * 1024, QChar('x'));
    const QJsonObject env = buildEnvelope(kp, "post", {{"post", post}});
    QVERIFY(!m_feed->ingestPost(env));
}

void TestFeedStore::ingestPost_rateLimiting_exceededRejected()
{
    // kMaxMsgsPerWindow = 100. Send 100 valid posts (each unique ID), then verify
    // the 101st is rejected for the same author within the same time window.
    const Keypair kp = subscribeAuthor();

    for (int i = 0; i < 100; ++i) {
        const QString id  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(id)}});
        QVERIFY2(m_feed->ingestPost(env),
                 qPrintable(QString("post %1 should have been accepted").arg(i)));
    }

    // 101st must be rejected by rate limiter
    const QString id101 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject env101 = buildEnvelope(kp, "post", {{"post", makePost(id101)}});
    QVERIFY(!m_feed->ingestPost(env101));
}

void TestFeedStore::ingestPost_perAuthorCap_exceededRejected()
{
    // kMaxPostsPerAuthor = 1000. Pre-populate the mock store with 1000 feed
    // entries for the author, then try to ingest a new (1001st) post.
    const Keypair kp = subscribeAuthor();

    for (int i = 0; i < 1000; ++i) {
        const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString key = "feed:" + kp.pubkeyHex + ":" + postId;
        QJsonObject fake;
        fake["id"]             = postId;
        fake["title"]          = QString("Post %1").arg(i);
        fake["author_pubkey"]  = kp.pubkeyHex;
        fake["created_at"]     = "2024-01-01T00:00:00Z";
        fake["updated_at"]     = "2024-01-01T00:00:00Z";
        m_kv->m_store[key] = QString::fromUtf8(QJsonDocument(fake).toJson(QJsonDocument::Compact));
    }

    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject env = buildEnvelope(kp, "post", {{"post", makePost(newId)}});
    QVERIFY(!m_feed->ingestPost(env));
}

// ── ingestDelete ──────────────────────────────────────────────────────────────

void TestFeedStore::ingestDelete_removesPost()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // First ingest the post
    QVERIFY(m_feed->ingestPost(buildEnvelope(kp, "post", {{"post", makePost(postId)}})));
    QVERIFY(!m_feed->getPost(kp.pubkeyHex, postId).isEmpty());

    // Now send a delete envelope
    QJsonObject del;
    del["post_id"] = postId;
    const QJsonObject env = buildEnvelope(kp, "delete", {{"delete", del}});
    QVERIFY(m_feed->ingestDelete(env));

    QVERIFY(m_feed->getPost(kp.pubkeyHex, postId).isEmpty());
}

void TestFeedStore::ingestDelete_rejectsBadSignature()
{
    const Keypair kp = subscribeAuthor();
    const QString postId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject del;
    del["post_id"] = postId;
    QJsonObject env = buildEnvelope(kp, "delete", {{"delete", del}});
    // Tamper with the payload after signing
    QJsonObject badDel;
    badDel["post_id"] = "other-id";
    env["delete"] = badDel;

    QVERIFY(!m_feed->ingestDelete(env));
}

// ── ingestProfile ─────────────────────────────────────────────────────────────

void TestFeedStore::ingestProfile_storesProfile()
{
    const Keypair kp = subscribeAuthor("Old Name");

    QJsonObject profile;
    profile["name"]       = "New Name";
    profile["bio"]        = "Bio text";
    profile["avatar_url"] = "";
    const QJsonObject env = buildEnvelope(kp, "profile", {{"profile", profile}});

    QVERIFY(m_feed->ingestProfile(env));

    // Profile stored under "profiles:<pubkey>"
    const QString key = "profiles:" + kp.pubkeyHex;
    QVERIFY(m_kv->m_store.contains(key));
    const QJsonObject stored =
        QJsonDocument::fromJson(m_kv->m_store[key].toUtf8()).object();
    QCOMPARE(stored["name"].toString(), QString("New Name"));
}

void TestFeedStore::ingestProfile_updatesSubscriptionName()
{
    const Keypair kp = subscribeAuthor("Old Name");

    QJsonObject profile;
    profile["name"] = "Updated Name";
    const QJsonObject env = buildEnvelope(kp, "profile", {{"profile", profile}});
    QVERIFY(m_feed->ingestProfile(env));

    // Subscription record should also reflect the new name
    const QString subKey = "subscriptions:" + kp.pubkeyHex;
    const QJsonObject sub =
        QJsonDocument::fromJson(m_kv->m_store[subKey].toUtf8()).object();
    QCOMPARE(sub["name"].toString(), QString("Updated Name"));
}

void TestFeedStore::ingestProfile_rejectsBadSignature()
{
    const Keypair kp = subscribeAuthor();

    QJsonObject profile;
    profile["name"] = "Name";
    QJsonObject env = buildEnvelope(kp, "profile", {{"profile", profile}});
    // Tamper after signing
    QJsonObject tampered;
    tampered["name"] = "Evil Name";
    env["profile"] = tampered;

    QVERIFY(!m_feed->ingestProfile(env));
}

// ── Queries ───────────────────────────────────────────────────────────────────

void TestFeedStore::getAggregatedFeed_mergesAuthors_sortedByDate()
{
    const Keypair kp1 = subscribeAuthor("Author 1");
    const Keypair kp2 = subscribeAuthor("Author 2");

    const QString id1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString id2 = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Author 1 has an older post, Author 2 has a newer post
    QJsonObject post1 = makePost(id1, "2024-01-01T00:00:00Z");
    post1["created_at"] = "2024-01-01T00:00:00Z";
    QJsonObject post2 = makePost(id2, "2024-06-01T00:00:00Z");
    post2["created_at"] = "2024-06-01T00:00:00Z";

    QVERIFY(m_feed->ingestPost(buildEnvelope(kp1, "post", {{"post", post1}})));
    QVERIFY(m_feed->ingestPost(buildEnvelope(kp2, "post", {{"post", post2}})));

    const QJsonArray feed = m_feed->getAggregatedFeed();
    QCOMPARE(feed.size(), 2);
    // Newer (id2 from author 2) must come first
    QCOMPARE(feed[0].toObject()["id"].toString(), id2);
    QCOMPARE(feed[1].toObject()["id"].toString(), id1);
}

void TestFeedStore::getPostsByTag_filtersCorrectly()
{
    const Keypair kp = subscribeAuthor();

    const QString idA = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString idB = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject postA = makePost(idA);
    postA["tags"] = QJsonArray{{"rust"}};
    QJsonObject postB = makePost(idB);
    postB["tags"] = QJsonArray{{"qt", "cpp"}};

    QVERIFY(m_feed->ingestPost(buildEnvelope(kp, "post", {{"post", postA}})));
    QVERIFY(m_feed->ingestPost(buildEnvelope(kp, "post", {{"post", postB}})));

    const QJsonArray rustPosts = m_feed->getPostsByTag("rust");
    QCOMPARE(rustPosts.size(), 1);
    QCOMPARE(rustPosts[0].toObject()["id"].toString(), idA);

    const QJsonArray qtPosts = m_feed->getPostsByTag("qt");
    QCOMPARE(qtPosts.size(), 1);
    QCOMPARE(qtPosts[0].toObject()["id"].toString(), idB);

    // Non-existent tag → empty
    QCOMPARE(m_feed->getPostsByTag("python").size(), 0);
}

QTEST_GUILESS_MAIN(TestFeedStore)
#include "test_feed_store.moc"
