#pragma once
#include <QObject>
#include <QWidget>
#include <QtPlugin>

class LogosAPI;

class IComponent {
public:
    virtual ~IComponent() = default;
    virtual QWidget* createWidget(LogosAPI* logosAPI = nullptr) = 0;
    virtual void destroyWidget(QWidget* widget) = 0;
};

#define IComponent_iid "com.logos.component.IComponent"
Q_DECLARE_INTERFACE(IComponent, IComponent_iid)

class BlogUIComponent : public QObject, public IComponent {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IComponent_iid FILE "ui_metadata.json")
    Q_INTERFACES(IComponent)
public:
    explicit BlogUIComponent(QObject* parent = nullptr);
    QWidget* createWidget(LogosAPI* logosAPI) override;
    void     destroyWidget(QWidget* widget) override;
};
