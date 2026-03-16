#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class LogosAPIClient;

class PostStore : public QObject {
    Q_OBJECT
public:
    explicit PostStore(QObject* parent = nullptr);

    void setKvClient(LogosAPIClient* kv);

    // Returns new post ID (uuid v4), saves as draft
    QString createDraft(const QString& title, const QString& body,
                        const QString& summary, const QStringList& tags);

    // Update existing draft or post; returns false if not found
    bool update(const QString& id, const QString& title, const QString& body,
                const QString& summary, const QStringList& tags);

    // Move draft -> post, sign placeholder, return post JSON
    QString publish(const QString& id);

    // Hard-delete from kv_module; returns tombstone JSON
    QString remove(const QString& id);

    // Accessors
    QJsonObject getPost(const QString& id);
    QJsonArray  listPosts();   // published only, sorted by created_at desc
    QJsonArray  listDrafts();  // drafts only

signals:
    void postCreated(const QString& id);
    void postUpdated(const QString& id);
    void postPublished(const QString& id, const QString& signedJson);
    void postRemoved(const QString& id);

private:
    LogosAPIClient* m_kv = nullptr;

    static constexpr const char* NS = "blog";

    void        savePost(const QJsonObject& post, bool draft);
    QJsonObject loadPost(const QString& id, bool draft);
    QJsonObject loadAnyPost(const QString& id, bool& isDraft);
    QString     generateUuid();
    QString     nowIso();
};
