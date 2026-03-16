#pragma once
#include <QtPlugin>
#include <QWidget>

class LogosAPI;

// IComponent — interface for Logos UI plugins that provide a QWidget view.
// This header is local to the blog plugin; a future SDK version will supply it.
class IComponent
{
public:
    virtual ~IComponent() {}
    virtual QWidget* createWidget(LogosAPI* api) = 0;
    virtual void     destroyWidget(QWidget* widget) = 0;
};

#define IComponent_iid "com.example.IComponent"
Q_DECLARE_INTERFACE(IComponent, IComponent_iid)
