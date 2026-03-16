#pragma once
// Minimal stub of the Logos SDK LogosAPIClient class.
// Used only in BUILD_TESTS builds — the real header comes from logos-cpp-sdk.
// Plain class (no QObject/Q_OBJECT) to avoid needing a separate moc step.

#include <QString>
#include <QVariant>

class LogosAPIClient {
public:
    LogosAPIClient() = default;
    virtual ~LogosAPIClient() = default;

    virtual QVariant invokeRemoteMethod(const QString& /*objectName*/,
                                        const QString& /*method*/,
                                        const QVariant& /*arg1*/ = QVariant(),
                                        const QVariant& /*arg2*/ = QVariant(),
                                        const QVariant& /*arg3*/ = QVariant())
    {
        return {};
    }
};
