#pragma once
#include "i_component.h"
#include <QtPlugin>

class BlogUIComponent : public QObject, public IComponent {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.example.IComponent" FILE "../ui_metadata.json")
    Q_INTERFACES(IComponent)
public:
    explicit BlogUIComponent(QObject* parent = nullptr);
    QWidget* createWidget(LogosAPI* logosAPI) override;
    void     destroyWidget(QWidget* widget) override;
};
