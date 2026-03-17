#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>

class LogosAPIClient;

// Interface to Logos Storage Module (org.logos.StorageModuleInterface).
// Handles content-addressed storage for blog post bodies.
//
// Publishing flow:
//   1. BlogPlugin calls uploadContent(postJson) → CID
//   2. CID is embedded in the Chat SDK envelope and stored in kv
//
// Receiving flow:
//   1. Subscriber receives Chat envelope containing CID
//   2. BlogPlugin calls downloadContent(cid) → full post JSON
//   3. Post is fed to FeedStore for ingestion
class StorageSync : public QObject {
    Q_OBJECT
public:
    explicit StorageSync(QObject* parent = nullptr);

    void setStorageClient(LogosAPIClient* storage);
    bool isAvailable() const { return m_storage != nullptr; }

    // Upload content bytes to storage.
    // Uses storage_module uploadUrl() method with a temporary local file.
    // Returns the CID string on success, or empty string on failure.
    QString uploadContent(const QByteArray& content);

    // Download content by CID.
    // Tries downloadToUrl() first, falls back to downloadChunks().
    // Returns raw bytes, or empty on failure.
    QByteArray downloadContent(const QString& cid);

    // Check whether a CID is locally available (exists() call).
    bool exists(const QString& cid);

    // Remove a CID from storage (e.g. on post deletion).
    bool remove(const QString& cid);

signals:
    void uploadComplete(const QString& cid);
    void downloadComplete(const QString& cid, const QByteArray& content);
    void storageError(const QString& error);

private:
    LogosAPIClient* m_storage = nullptr;
};
