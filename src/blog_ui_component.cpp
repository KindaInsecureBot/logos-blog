#include "blog_ui_component.h"
#include "blog_backend.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QUrl>

BlogUIComponent::BlogUIComponent(QObject* parent)
    : QObject(parent)
{}

QWidget* BlogUIComponent::createWidget(LogosAPI* logosAPI)
{
    auto* quickWidget = new QQuickWidget();
    quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);

    auto* backend = new BlogBackend();
    backend->setParent(quickWidget);

#ifdef LOGOS_CORE_AVAILABLE
    if (logosAPI) {
        backend->initLogos(logosAPI);
    }
#endif

    // Set context property BEFORE setSource — QML engine reads it at load time
    quickWidget->rootContext()->setContextProperty("backend", backend);
    quickWidget->setSource(QUrl("qrc:/blog_ui/Main.qml"));

    return quickWidget;
}

void BlogUIComponent::destroyWidget(QWidget* widget)
{
    delete widget;
}
