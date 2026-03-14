#pragma once
// Minimal stub of the Logos SDK ModuleProxy class.
// Used only in BUILD_TESTS builds — the real header comes from logos-cpp-sdk.
// Plain class (no QObject/Q_OBJECT) to avoid needing a separate moc step.

#include <QString>
#include <QVariant>

class ModuleProxy {
public:
    ModuleProxy() = default;
    virtual ~ModuleProxy() = default;

    virtual QVariant invokeRemoteMethod(const QString& /*module*/,
                                         const QString& /*method*/,
                                         const QString& /*arg1*/ = QString(),
                                         const QString& /*arg2*/ = QString(),
                                         const QString& /*arg3*/ = QString())
    {
        return {};
    }
};
