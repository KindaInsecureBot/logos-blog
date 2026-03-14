import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root

    // Injected by BlogUIComponent::createWidget via rootContext setContextProperty
    // property var backend  -- provided by C++ context

    readonly property color colorBg:       "#1a1b1e"
    readonly property color colorSurface:  "#25262b"
    readonly property color colorBorder:   "#373a40"
    readonly property color colorPrimary:  "#4dabf7"
    readonly property color colorText:     "#c1c2c5"
    readonly property color colorTextDim:  "#5c5f66"
    readonly property color colorAccent:   "#339af0"

    anchors.fill: parent

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left sidebar ──────────────────────────────────────────────────
        Rectangle {
            id: sidebar
            Layout.preferredWidth: 56
            Layout.fillHeight: true
            color: colorSurface

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: colorBorder
            }

            Column {
                anchors {
                    top: parent.top
                    horizontalCenter: parent.horizontalCenter
                    topMargin: 8
                }
                spacing: 4

                SidebarButton {
                    icon: "F"
                    tooltip: "Feed"
                    active: stackView.currentItem && stackView.currentItem.viewId === "feed"
                    onClicked: stackView.navigateTo("feed")
                }
                SidebarButton {
                    icon: "M"
                    tooltip: "My Posts"
                    active: stackView.currentItem && stackView.currentItem.viewId === "myposts"
                    onClicked: stackView.navigateTo("myposts")
                }
                SidebarButton {
                    icon: "D"
                    tooltip: "Drafts"
                    active: stackView.currentItem && stackView.currentItem.viewId === "drafts"
                    onClicked: stackView.navigateTo("drafts")
                }
                SidebarButton {
                    icon: "+"
                    tooltip: "New Post"
                    active: stackView.currentItem && stackView.currentItem.viewId === "editor"
                    onClicked: stackView.navigateTo("editor")
                }
                SidebarButton {
                    icon: "S"
                    tooltip: "Settings"
                    active: stackView.currentItem && stackView.currentItem.viewId === "settings"
                    onClicked: stackView.navigateTo("settings")
                }
            }
        }

        // ── Main content area ─────────────────────────────────────────────
        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: feedViewComponent

            function navigateTo(viewId) {
                const item = stackView.currentItem
                if (item && item.viewId === viewId) return

                let comp
                switch(viewId) {
                    case "feed":     comp = feedViewComponent;    break
                    case "myposts":  comp = myPostsViewComponent; break
                    case "drafts":   comp = draftsViewComponent;  break
                    case "editor":   comp = editorViewComponent;  break
                    case "settings": comp = settingsViewComponent; break
                    default: return
                }
                stackView.replace(comp)
            }

            // Keep depth at 1 for sidebar navigation; use push for drill-down
            function openPost(postId, authorPubkey) {
                stackView.push(postViewComponent, {
                    "postId": postId,
                    "authorPubkey": authorPubkey
                })
            }
        }
    }

    // ── Error banner (floating at bottom) ─────────────────────────────────
    ErrorBanner {
        id: errorBanner
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            leftMargin: 64
            rightMargin: 8
            bottomMargin: 8
        }
    }

    // ── Backend signal connections ─────────────────────────────────────────
    Connections {
        target: typeof backend !== "undefined" ? backend : null
        function onErrorOccurred(msg) { errorBanner.show(msg) }
        function onPostReceived(postJson) {
            const item = stackView.currentItem
            if (item && typeof item.refresh === "function") item.refresh()
        }
        function onPostDeleted(postId, authorPubkey) {
            const item = stackView.currentItem
            if (item && typeof item.refresh === "function") item.refresh()
        }
    }

    // ── View components ───────────────────────────────────────────────────
    Component { id: feedViewComponent;     FeedView    { onOpenPost: (id, pk) => stackView.openPost(id, pk) } }
    Component { id: myPostsViewComponent;  MyPostsView { onOpenPost: (id) => stackView.openPost(id, "") onNewPost: stackView.navigateTo("editor") } }
    Component { id: draftsViewComponent;   DraftsView  { onEditDraft: (id) => { stackView.push(editorViewComponent, {"draftId": id}) } } }
    Component { id: editorViewComponent;   EditorView  { onPostPublished: (id) => { stackView.navigateTo("myposts") } } }
    Component { id: settingsViewComponent; SettingsView {} }
    Component { id: postViewComponent;     PostView    {} }
}
