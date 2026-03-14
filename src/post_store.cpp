#include "post_store.h"
#include "module_proxy.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <algorithm>

PostStore::PostStore(QObject* parent)
    : QObject(parent)
{}

void PostStore::setKvClient(ModuleProxy* kv)
{
    m_kv = kv;
}

QString PostStore::generateUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString PostStore::nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

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
        // Already published — try to return existing
        post = loadPost(id, /*draft=*/false);
        if (post.isEmpty()) return {};
        return QString::fromUtf8(QJsonDocument(post).toJson(QJsonDocument::Compact));
    }

    // Remove draft
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("drafts:") + id);

    // Promote to published
    const QString now = nowIso();
    post["published"]  = true;
    post["updated_at"] = now;
    post["signature"]  = QString(); // placeholder — real Ed25519 in Phase 3

    savePost(post, /*draft=*/false);

    const QString signedJson = QString::fromUtf8(
        QJsonDocument(post).toJson(QJsonDocument::Compact));
    emit postPublished(id, signedJson);
    return signedJson;
}

QString PostStore::remove(const QString& id)
{
    if (!m_kv) return {};

    // Try to delete from both namespaces
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("posts:")  + id);
    m_kv->invokeRemoteMethod("kv_module", "remove",
                             QString(NS), QStringLiteral("drafts:") + id);

    // Return tombstone JSON for Waku broadcast (Phase 3)
    QJsonObject tombstone;
    tombstone["type"]    = "delete";
    tombstone["post_id"] = id;
    const QString json = QString::fromUtf8(
        QJsonDocument(tombstone).toJson(QJsonDocument::Compact));

    emit postRemoved(id);
    return json;
}

QJsonObject PostStore::getPost(const QString& id)
{
    // Check published first, then drafts
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
        if (!key.startsWith("posts:")) continue;

        const QString val = entry["value"].toString();
        const QJsonObject post = QJsonDocument::fromJson(val.toUtf8()).object();
        if (!post.isEmpty() && post["published"].toBool()) {
            posts.append(post);
        }
    }

    // Sort by created_at descending
    std::sort(posts.begin(), posts.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["created_at"].toString() > b.toObject()["created_at"].toString();
    });

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

    // Sort by updated_at descending
    std::sort(drafts.begin(), drafts.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["updated_at"].toString() > b.toObject()["updated_at"].toString();
    });

    return drafts;
}
