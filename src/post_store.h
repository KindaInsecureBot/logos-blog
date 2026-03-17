#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class ModuleProxy;

class PostStore : public QObject {
    Q_OBJECT
public:
    explicit PostStore(QObject* parent = nullptr);

    void setKvClient(ModuleProxy* kv);
    void setStorageClient(ModuleProxy* storage);

    // Returns new post ID (uuid v4), saves as draft
    QString createDraft(const QString& title, const QString& body,
                        const QString& summary, const QStringList& tags);

    // Update existing draft or post; returns false if not found
    bool update(const QString& id, const QString& title, const QString& body,
                const QString& summary, const QStringList& tags);

    // Move draft -> posts namespace, upload content to Storage module,
    // store CID→metadata in KV for indexing. Returns post JSON or empty on error.
    QString publish(const QString& id);

    // Hard-delete from kv_module; returns tombstone JSON
    QString remove(const QString& id);

    // Returns CID for a published post (empty if not published or no storage)
    QString getPostCid(const QString& id) const;

    // Fetch full post: checks CID in KV, downloads from Storage; falls back to KV
    QJsonObject getPost(const QString& id);
    QJsonArray  listPosts();   // metadata only (no body), sorted by created_at desc
    QJsonArray  listDrafts();  // drafts only

signals:
    void postCreated(const QString& id);
    void postUpdated(const QString& id);
    // cid is the Storage CID (empty if storage_module unavailable)
    void postPublished(const QString& id, const QString& cid);
    void postRemoved(const QString& id);

private:
    ModuleProxy* m_kv      = nullptr;
    ModuleProxy* m_storage = nullptr;

    static constexpr const char* NS = "blog";

    // Upload UTF-8 content to storage_module via a temp file.
    // Returns CID string, or empty on failure.
    QString uploadToStorage(const QString& content);

    // Download content by CID from storage_module.
    // Returns the raw content string, or empty on failure.
    QString downloadFromStorage(const QString& cid);

    void        savePost(const QJsonObject& post, bool draft);
    QJsonObject loadPost(const QString& id, bool draft);
    QJsonObject loadAnyPost(const QString& id, bool& isDraft);
    QString     generateUuid();
    QString     nowIso();
};
