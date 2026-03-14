// tests/test_post_store.cpp
// Unit tests for PostStore CRUD operations using an in-memory MockKvClient.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QRegularExpression>

#include "mock_kv_client.h"
#include "post_store.h"

// Helper: directly set a field in a stored JSON object inside the mock.
static void patchStoredJson(MockKvClient* kv, const QString& key,
                             const QString& field, const QJsonValue& val)
{
    const QString raw = kv->m_store.value(key);
    QJsonObject obj = QJsonDocument::fromJson(raw.toUtf8()).object();
    obj[field] = val;
    kv->m_store[key] = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

class TestPostStore : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // createDraft
    void createDraft_returnsValidUuid();
    void createDraft_storedAsDraft_notPost();
    void createDraft_fieldsCorrect();

    // update
    void updatePost_titleBodyTagsChanged();
    void updatePost_updatedAtAdvances();
    void updatePost_returnsFalseForMissingId();

    // publish
    void publishDraft_removesFromDrafts();
    void publishDraft_addsToPublished();
    void publishDraft_setsPublishedTrue();
    void publishDraft_alreadyPublished_returnsJson();

    // remove
    void deletePost_removedFromPublished();
    void deletePost_removedFromDrafts();

    // listPosts / listDrafts
    void listPosts_onlyPublished();
    void listDrafts_onlyDrafts();

    // getPost
    void getPost_findsPublished();
    void getPost_findsDraft();
    void getPost_emptyForNonexistent();

    // sorting
    void listPosts_sortedByCreatedAtDesc();

private:
    MockKvClient* m_kv    = nullptr;
    PostStore*    m_store = nullptr;
};

void TestPostStore::init()
{
    m_kv    = new MockKvClient();
    m_store = new PostStore();
    m_store->setKvClient(m_kv);
}

void TestPostStore::cleanup()
{
    delete m_store;
    delete m_kv;
    m_store = nullptr;
    m_kv    = nullptr;
}

// ── createDraft ───────────────────────────────────────────────────────────────

void TestPostStore::createDraft_returnsValidUuid()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    QVERIFY(!id.isEmpty());
    // QUuid parses a valid UUID and returns non-null
    QVERIFY(!QUuid::fromString(id).isNull());
}

void TestPostStore::createDraft_storedAsDraft_notPost()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    QVERIFY( m_kv->m_store.contains("drafts:" + id));
    QVERIFY(!m_kv->m_store.contains("posts:"  + id));
}

void TestPostStore::createDraft_fieldsCorrect()
{
    const QString id = m_store->createDraft("My Title", "My Body", "My Summary",
                                             {"tagA", "tagB"});
    const QJsonObject post = m_store->getPost(id);

    QCOMPARE(post["id"].toString(),      id);
    QCOMPARE(post["title"].toString(),   QString("My Title"));
    QCOMPARE(post["body"].toString(),    QString("My Body"));
    QCOMPARE(post["summary"].toString(), QString("My Summary"));
    QVERIFY(!post["created_at"].toString().isEmpty());
    QVERIFY(!post["updated_at"].toString().isEmpty());
    QVERIFY(!post["published"].toBool());

    const QJsonArray tags = post["tags"].toArray();
    QCOMPARE(tags.size(), 2);
    QCOMPARE(tags[0].toString(), QString("tagA"));
    QCOMPARE(tags[1].toString(), QString("tagB"));
}

// ── update ────────────────────────────────────────────────────────────────────

void TestPostStore::updatePost_titleBodyTagsChanged()
{
    const QString id = m_store->createDraft("Old", "Old body", "Old sum", {"old"});
    const bool ok = m_store->update(id, "New", "New body", "New sum", {"new1", "new2"});
    QVERIFY(ok);

    const QJsonObject post = m_store->getPost(id);
    QCOMPARE(post["title"].toString(),   QString("New"));
    QCOMPARE(post["body"].toString(),    QString("New body"));
    QCOMPARE(post["summary"].toString(), QString("New sum"));

    const QJsonArray tags = post["tags"].toArray();
    QCOMPARE(tags.size(), 2);
    QCOMPARE(tags[0].toString(), QString("new1"));
}

void TestPostStore::updatePost_updatedAtAdvances()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    // Backdate the stored updated_at to a known past time
    patchStoredJson(m_kv, "drafts:" + id, "updated_at", "2020-01-01T00:00:00Z");

    m_store->update(id, "T2", "B2", "S2", {});
    const QJsonObject post = m_store->getPost(id);

    // updated_at must now be after our backdated value
    QVERIFY(post["updated_at"].toString() > "2020-01-01T00:00:00Z");
}

void TestPostStore::updatePost_returnsFalseForMissingId()
{
    QVERIFY(!m_store->update("no-such-id", "T", "B", "S", {}));
}

// ── publish ───────────────────────────────────────────────────────────────────

void TestPostStore::publishDraft_removesFromDrafts()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    QVERIFY(m_kv->m_store.contains("drafts:" + id));
    m_store->publish(id);
    QVERIFY(!m_kv->m_store.contains("drafts:" + id));
}

void TestPostStore::publishDraft_addsToPublished()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    m_store->publish(id);
    QVERIFY(m_kv->m_store.contains("posts:" + id));
}

void TestPostStore::publishDraft_setsPublishedTrue()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    m_store->publish(id);
    QVERIFY(m_store->getPost(id)["published"].toBool());
}

void TestPostStore::publishDraft_alreadyPublished_returnsJson()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    const QString first  = m_store->publish(id);
    const QString second = m_store->publish(id);   // already published
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
}

// ── remove ────────────────────────────────────────────────────────────────────

void TestPostStore::deletePost_removedFromPublished()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    m_store->publish(id);
    QVERIFY(m_kv->m_store.contains("posts:" + id));
    m_store->remove(id);
    QVERIFY(!m_kv->m_store.contains("posts:" + id));
}

void TestPostStore::deletePost_removedFromDrafts()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    QVERIFY(m_kv->m_store.contains("drafts:" + id));
    m_store->remove(id);
    QVERIFY(!m_kv->m_store.contains("drafts:" + id));
}

// ── listPosts / listDrafts ────────────────────────────────────────────────────

void TestPostStore::listPosts_onlyPublished()
{
    const QString draft = m_store->createDraft("Draft",     "B", "S", {});
    const QString pub   = m_store->createDraft("Published", "B", "S", {});
    m_store->publish(pub);

    const QJsonArray posts = m_store->listPosts();
    QCOMPARE(posts.size(), 1);
    QCOMPARE(posts[0].toObject()["id"].toString(), pub);
}

void TestPostStore::listDrafts_onlyDrafts()
{
    const QString draft = m_store->createDraft("Draft",     "B", "S", {});
    const QString pub   = m_store->createDraft("Published", "B", "S", {});
    m_store->publish(pub);

    const QJsonArray drafts = m_store->listDrafts();
    QCOMPARE(drafts.size(), 1);
    QCOMPARE(drafts[0].toObject()["id"].toString(), draft);
}

// ── getPost ───────────────────────────────────────────────────────────────────

void TestPostStore::getPost_findsPublished()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    m_store->publish(id);
    QCOMPARE(m_store->getPost(id)["id"].toString(), id);
}

void TestPostStore::getPost_findsDraft()
{
    const QString id = m_store->createDraft("T", "B", "S", {});
    QCOMPARE(m_store->getPost(id)["id"].toString(), id);
}

void TestPostStore::getPost_emptyForNonexistent()
{
    QVERIFY(m_store->getPost("nonexistent-uuid").isEmpty());
}

// ── sorting ───────────────────────────────────────────────────────────────────

void TestPostStore::listPosts_sortedByCreatedAtDesc()
{
    const QString id1 = m_store->createDraft("Post 1", "B", "S", {});
    const QString id2 = m_store->createDraft("Post 2", "B", "S", {});

    // Assign deterministic created_at values directly in the store
    patchStoredJson(m_kv, "drafts:" + id1, "created_at", "2024-01-01T00:00:00Z");
    patchStoredJson(m_kv, "drafts:" + id2, "created_at", "2024-06-01T00:00:00Z");

    m_store->publish(id1);
    m_store->publish(id2);

    const QJsonArray posts = m_store->listPosts();
    QCOMPARE(posts.size(), 2);
    // Newer (id2) should come first
    QCOMPARE(posts[0].toObject()["id"].toString(), id2);
    QCOMPARE(posts[1].toObject()["id"].toString(), id1);
}

QTEST_GUILESS_MAIN(TestPostStore)
#include "test_post_store.moc"
