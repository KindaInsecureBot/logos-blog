#include "post_store.h"
#include "logos_api_client.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <QTemporaryFile>
#include <QFile>
#include <QUrl>
#include <algorithm>

PostStore::PostStore(QObject* parent)
    : QObject(parent)
{}

void PostStore::setKvClient(ModuleProxy* kv)
{
    m_kv = kv;
}

void PostStore::setStorageClient(ModuleProxy* storage)
{
    m_storage = storage;
}

QString PostStore::generateUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString PostStore::nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// ── Storage helpers ────────────────────────────────────────────────────────────

QString PostStore::uploadToStorage(const QString& content)
{
    if (!m_storage) return {};

    // Write content to a temporary file; uploadUrl() requires a file URL
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    tmp.write(content.toUtf8());
    tmp.flush();
    const QString filePath = tmp.fileName();
    tmp.close();

    const QString fileUrl = QUrl::fromLocalFile(filePath).toString();
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "uploadUrl", fileUrl, 256 * 1024);

    QFile::remove(filePath);

    return result.toString();
}

QString PostStore::downloadFromStorage(const QString& cid)
{
    if (!m_storage || cid.isEmpty()) return {};
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "downloadChunks", cid);
    return result.toString();
}

// ── KV helpers ────────────────────────────────────────────────────────────────

void PostStore::savePost(const QJsonObject& post, bool draft)
{
    if (!m_kv) return;
    const QString id  = post["id"].toString();
    const QString key = draft ? QStringLiteral("drafts:") + id
                              : QStringLiteral("posts:")  + id;
    const QString val = QString::fromUtf8(
        QJsonDocument(post).toJson(QJsonDocument::Compact));
    m_kv->invokeRemoteMethod("kv_module", "set", QString(NS), key, val);
}

QJsonObject PostStore::loadPost(const QString& id, bool draft)
{
    if (!m_kv) return {};
    const QString key = draft ? QStringLiteral("drafts:") + id
                              : QStringLiteral("posts:")  + id;
    QVariant result = m_kv->invokeRemoteMethod("kv_module", "get", QString(NS), key);
    const QString json = result.toString();
    if (json.isEmpty()) return {};
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

QJsonObject PostStore::loadAnyPost(const QString& id, bool& isDraft)
{
    QJsonObject obj = loadPost(id, false);
    if (!obj.isEmpty()) { isDraft = false; return obj; }
    obj = loadPost(id, true);
    isDraft = !obj.isEmpty();
    return obj;
}

// ── Public API ────────────────────────────────────────────────────────────────

QString PostStore::createDraft(const QString& title, const QString& body,
                               const QString& summary, const QStringList& tags)
{
    const QString id  = generateUuid();
    const QString now = nowIso();

    QJsonArray tagArr;
    for (const auto& t : tags) tagArr.append(t);

    QJsonObject post;
    post["id"]         = id;
    post["title"]      = title;
    post["body"]       = body;
    post["summary"]    = summary;
    post["tags"]       = tagArr;
    post["created_at"] = now;
    post["updated_at"] = now;
    post["published"]  = false;
    post["signature"]  = QString();

    savePost(post, /*draft=*/true);
    emit postCreated(id);
    return id;
}

bool PostStore::update(const QString& id, const QString& title, const QString& body,
                       const QString& summary, const QStringList& tags)
{
    if (!m_kv) return false;

    bool isDraft = false;
    QJsonObject post = loadAnyPost(id, isDraft);
    if (post.isEmpty()) return false;

    QJsonArray tagArr;
    for (const auto& t : tags) tagArr.append(t);

    post["title"]      = title;
    post["body"]       = body;
    post["summary"]    = summary;
    post["tags"]       = tagArr;
    post["updated_at"] = nowIso();

    savePost(post, isDraft);
    emit postUpdated(id);
    return true;
}

QString PostStore::publish(const QString& id)
{
    if (!m_kv) return {};

    QJsonObject post = loadPost(id, /*draft=*/true);
    if (post.isEmpty()) {
        // Already published — return existing CID or JSON
        post = loadPost(id, /*draft=*/false);
        if (post.isEmpty()) return {};
        const QString cid = getPostCid(id);
        emit postPublished(id, cid);
        return QString::fromUtf8(QJsonDocument(post).toJson(QJsonDocument::Compact));
    }

    // Remove draft from KV
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("drafts:") + id);

    const QString now = nowIso();
    post["published"]    = true;
    post["updated_at"]   = now;
    post["published_at"] = now;
    post["signature"]    = QString();

    // Upload full post JSON to Storage module; fall back to KV-only if unavailable
    const QString postJson = QString::fromUtf8(
        QJsonDocument(post).toJson(QJsonDocument::Compact));
    const QString cid = uploadToStorage(postJson);

    if (!cid.isEmpty()) {
        // Store CID → id mapping for reverse lookup
        m_kv->invokeRemoteMethod("kv_module", "set",
            QString(NS), QStringLiteral("cid:") + id, cid);

        // Store lightweight metadata for fast listing (no body)
        QJsonObject meta;
        meta["id"]           = post["id"];
        meta["title"]        = post["title"];
        meta["summary"]      = post["summary"];
        meta["tags"]         = post["tags"];
        meta["created_at"]   = post["created_at"];
        meta["published_at"] = post["published_at"];
        meta["cid"]          = cid;
        m_kv->invokeRemoteMethod("kv_module", "set",
            QString(NS), QStringLiteral("meta:") + id,
            QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)));
    } else {
        // No storage module: fall back to storing full post in KV
        savePost(post, /*draft=*/false);
    }

    emit postPublished(id, cid);
    return postJson;
}

QString PostStore::remove(const QString& id)
{
    if (!m_kv) return {};

    // Remove all KV entries for this post
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("posts:")  + id);
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("drafts:") + id);
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("cid:")    + id);
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("meta:")   + id);

    // Note: Storage content is content-addressed and immutable; we only remove
    // the local index reference, not the stored bytes themselves.

    QJsonObject tombstone;
    tombstone["type"]    = "delete";
    tombstone["post_id"] = id;
    const QString json = QString::fromUtf8(
        QJsonDocument(tombstone).toJson(QJsonDocument::Compact));

    emit postRemoved(id);
    return json;
}

QString PostStore::getPostCid(const QString& id) const
{
    if (!m_kv) return {};
    const QVariant result = m_kv->invokeRemoteMethod(
        "kv_module", "get", QString(NS), QStringLiteral("cid:") + id);
    return result.toString();
}

QJsonObject PostStore::getPost(const QString& id)
{
    // Try Storage path first (published posts)
    if (m_kv) {
        const QString cid = getPostCid(id);
        if (!cid.isEmpty()) {
            const QString content = downloadFromStorage(cid);
            if (!content.isEmpty()) {
                const QJsonObject post = QJsonDocument::fromJson(content.toUtf8()).object();
                if (!post.isEmpty()) return post;
            }
        }
    }

    // Fall back to KV (legacy published or draft)
    QJsonObject post = loadPost(id, /*draft=*/false);
    if (!post.isEmpty()) return post;
    return loadPost(id, /*draft=*/true);
}

QJsonArray PostStore::listPosts()
{
    if (!m_kv) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    const QJsonArray allItems = QJsonDocument::fromJson(json.toUtf8()).array();
    QJsonArray posts;

    for (const auto& item : allItems) {
        const QJsonObject entry = item.toObject();
        const QString key = entry["key"].toString();

        // Prefer metadata entries (Storage path); fall back to full KV posts
        if (key.startsWith("meta:")) {
            const QString val = entry["value"].toString();
            const QJsonObject meta = QJsonDocument::fromJson(val.toUtf8()).object();
            if (!meta.isEmpty()) posts.append(meta);
        } else if (key.startsWith("posts:")) {
            // KV fallback: only include if no meta entry exists
            const QString postId = key.mid(6);
            const QVariant metaVal = m_kv->invokeRemoteMethod(
                "kv_module", "get", QString(NS), QStringLiteral("meta:") + postId);
            if (metaVal.toString().isEmpty()) {
                const QString val = entry["value"].toString();
                const QJsonObject post = QJsonDocument::fromJson(val.toUtf8()).object();
                if (!post.isEmpty() && post["published"].toBool()) posts.append(post);
            }
        }
    }

    QList<QJsonValue> sorted(posts.begin(), posts.end());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });
    posts = QJsonArray();
    for (const auto& v : sorted) posts.append(v);

    return posts;
}

QJsonArray PostStore::listDrafts()
{
    if (!m_kv) return {};

    QVariant result = m_kv->invokeRemoteMethod("kv_module", "listAll", QString(NS));
    const QString json = result.toString();
    if (json.isEmpty()) return {};

    const QJsonArray allItems = QJsonDocument::fromJson(json.toUtf8()).array();
    QJsonArray drafts;

    for (const auto& item : allItems) {
        const QJsonObject entry = item.toObject();
        const QString key = entry["key"].toString();
        if (!key.startsWith("drafts:")) continue;

        const QString val = entry["value"].toString();
        const QJsonObject post = QJsonDocument::fromJson(val.toUtf8()).object();
        if (!post.isEmpty()) {
            drafts.append(post);
        }
    }

    QList<QJsonValue> sorted(drafts.begin(), drafts.end());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["updated_at"].toString() > b.toObject()["updated_at"].toString();
    });
    drafts = QJsonArray();
    for (const auto& v : sorted) drafts.append(v);

    return drafts;
}
