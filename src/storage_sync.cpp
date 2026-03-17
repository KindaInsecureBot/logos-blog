#include "storage_sync.h"
#include "logos_api_client.h"

#include <QFile>
#include <QTemporaryFile>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>

StorageSync::StorageSync(QObject* parent)
    : QObject(parent)
{}

void StorageSync::setStorageClient(LogosAPIClient* storage)
{
    m_storage = storage;
}

QString StorageSync::uploadContent(const QByteArray& content)
{
    if (!m_storage) return {};

    // Write content to a temporary file so we can pass a QUrl to uploadUrl().
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        emit storageError("StorageSync: failed to create temporary upload file");
        return {};
    }
    tmp.write(content);
    tmp.close();

    const QUrl fileUrl = QUrl::fromLocalFile(tmp.fileName());
    // uploadUrl(localFileUrl, chunkSize) → returns CID string.
    // Default chunk size: 256 KB.
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "uploadUrl", fileUrl.toString(), 256 * 1024);

    QFile::remove(tmp.fileName());

    const QString cid = result.toString().trimmed();
    if (cid.isEmpty()) {
        emit storageError("StorageSync: storage module returned empty CID");
        return {};
    }

    emit uploadComplete(cid);
    return cid;
}

QByteArray StorageSync::downloadContent(const QString& cid)
{
    if (!m_storage || cid.isEmpty()) return {};

    // Strategy 1: downloadToUrl(cid, destUrl) — writes to a local temp file.
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (tmp.open()) {
        const QString tmpPath = tmp.fileName();
        tmp.close();

        const QUrl destUrl = QUrl::fromLocalFile(tmpPath);
        // downloadToUrl is synchronous in the SDK proxy; result indicates success.
        // TODO: Update if the SDK switches to async completion callbacks.
        m_storage->invokeRemoteMethod(
            "storage_module", "downloadToUrl", cid, destUrl.toString());

        QFile f(tmpPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            f.close();
            QFile::remove(tmpPath);
            if (!data.isEmpty()) {
                emit downloadComplete(cid, data);
                return data;
            }
        }
        QFile::remove(tmpPath);
    }

    // Strategy 2: downloadChunks(cid) → JSON array of base64-encoded chunks.
    const QVariant chunksResult = m_storage->invokeRemoteMethod(
        "storage_module", "downloadChunks", cid);
    const QString chunksJson = chunksResult.toString();
    if (!chunksJson.isEmpty()) {
        const QJsonArray chunks = QJsonDocument::fromJson(chunksJson.toUtf8()).array();
        QByteArray assembled;
        for (const auto& chunk : chunks) {
            assembled += QByteArray::fromBase64(chunk.toString().toLatin1());
        }
        if (!assembled.isEmpty()) {
            emit downloadComplete(cid, assembled);
            return assembled;
        }
    }

    emit storageError("StorageSync: failed to download content for CID: " + cid);
    return {};
}

bool StorageSync::exists(const QString& cid)
{
    if (!m_storage || cid.isEmpty()) return false;
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "exists", cid);
    return result.toBool();
}

bool StorageSync::remove(const QString& cid)
{
    if (!m_storage || cid.isEmpty()) return false;
    const QVariant result = m_storage->invokeRemoteMethod(
        "storage_module", "remove", cid);
    return result.toBool();
}
