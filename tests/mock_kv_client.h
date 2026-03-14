#pragma once
// In-memory MockKvClient that satisfies the ModuleProxy interface used by
// PostStore and FeedStore. Supports: set, get, listAll, remove.

#include "module_proxy.h"

#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

class MockKvClient : public ModuleProxy {
public:
    MockKvClient() = default;

    // Public so tests can inspect and pre-populate the store directly.
    QMap<QString, QString> m_store;

    QVariant invokeRemoteMethod(const QString& /*module*/,
                                 const QString& method,
                                 const QString& /*ns*/,
                                 const QString& key   = QString(),
                                 const QString& value = QString()) override
    {
        if (method == "set") {
            m_store[key] = value;
            return {};
        }
        if (method == "get") {
            return m_store.value(key, QString());
        }
        if (method == "remove") {
            m_store.remove(key);
            return {};
        }
        if (method == "listAll") {
            QJsonArray arr;
            for (auto it = m_store.cbegin(); it != m_store.cend(); ++it) {
                QJsonObject entry;
                entry["key"]   = it.key();
                entry["value"] = it.value();
                arr.append(entry);
            }
            return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        }
        // Silently ignore other methods (e.g. setDataDir, createNode, start, …)
        return {};
    }
};
