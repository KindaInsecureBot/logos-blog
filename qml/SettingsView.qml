import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import "components"

Item {
    id: root
    readonly property string viewId: "settings"

    property var subscriptions: []

    function refreshSubs() {
        const json = backend.listSubscriptions()
        root.subscriptions = json ? JSON.parse(json) : []
    }

    Component.onCompleted: refreshSubs()

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ScrollView {
            anchors.fill: parent
            clip: true

            ColumnLayout {
                width: Math.min(parent.width, 600)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 24
                spacing: 24

                Text {
                    text: "Settings"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                // ── Own pubkey section ────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: "Your Identity Key"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: pubkeyContent.implicitHeight + 24
                        color: "#25262b"
                        radius: 8
                        border.color: "#373a40"

                        ColumnLayout {
                            id: pubkeyContent
                            anchors { fill: parent; margins: 16 }
                            spacing: 8

                            Text {
                                text: "Public Key (share this so others can subscribe to you)"
                                color: "#909296"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 32
                                    radius: 4
                                    color: "#1e1f22"
                                    border.color: "#373a40"

                                    Text {
                                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                        verticalAlignment: Text.AlignVCenter
                                        text: (typeof backend !== "undefined" && backend.ownPubkey)
                                            ? backend.ownPubkey
                                            : "Generating…"
                                        color: backend && backend.ownPubkey ? "#c1c2c5" : "#5c5f66"
                                        font.family: "monospace"
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                }

                                Button {
                                    text: "Copy"
                                    enabled: typeof backend !== "undefined" && backend.ownPubkey !== ""
                                    onClicked: {
                                        // Qt.clipboard not available in all environments;
                                        // use a TextEdit trick
                                        copyHelper.text = backend.ownPubkey
                                        copyHelper.selectAll()
                                        copyHelper.copy()
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#2a2d32" : "#25262b"
                                        border.color: "#373a40"
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#c1c2c5"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }

                            // Hidden TextEdit used only for clipboard copy
                            TextEdit {
                                id: copyHelper
                                visible: false
                                text: ""
                            }

                            Connections {
                                target: typeof backend !== "undefined" ? backend : null
                                function onIdentityChanged() {
                                    // Nothing to do — ownPubkey property binding updates automatically
                                }
                            }
                        }
                    }
                }

                // ── Identity section ──────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: "Profile"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: identityContent.implicitHeight + 24
                        color: "#25262b"
                        radius: 8
                        border.color: "#373a40"

                        ColumnLayout {
                            id: identityContent
                            anchors { fill: parent; margins: 16 }
                            spacing: 10

                            Text {
                                text: "Display Name"
                                color: "#909296"
                                font.pixelSize: 12
                            }
                            TextField {
                                id: nameField
                                Layout.fillWidth: true
                                text: typeof backend !== "undefined" ? backend.displayName : ""
                                placeholderText: "Your display name"
                                color: "#c1c2c5"
                                placeholderTextColor: "#5c5f66"
                                background: Rectangle {
                                    radius: 4
                                    color: "#1e1f22"
                                    border.color: parent.activeFocus ? "#4dabf7" : "#373a40"
                                }
                                leftPadding: 10
                            }

                            Text {
                                text: "Bio"
                                color: "#909296"
                                font.pixelSize: 12
                            }
                            TextField {
                                id: bioField
                                Layout.fillWidth: true
                                text: typeof backend !== "undefined" ? backend.bio : ""
                                placeholderText: "Short bio (optional)"
                                color: "#c1c2c5"
                                placeholderTextColor: "#5c5f66"
                                background: Rectangle {
                                    radius: 4
                                    color: "#1e1f22"
                                    border.color: parent.activeFocus ? "#4dabf7" : "#373a40"
                                }
                                leftPadding: 10
                            }

                            Button {
                                text: "Save Identity"
                                onClicked: backend.setIdentity(nameField.text, bioField.text)
                                background: Rectangle {
                                    radius: 4
                                    color: parent.hovered ? "#2c5282" : "#1e3a5f"
                                    border.color: "#4dabf7"
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: "#4dabf7"
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }
                }

                // ── Subscriptions section ─────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Text { text: "Subscriptions"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "+ Subscribe"
                            onClicked: subscribeDialog.open()
                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? "#2c5282" : "#1e3a5f"
                                border.color: "#4dabf7"
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "#4dabf7"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: Math.max(60, subList.count * 52 + 16)
                        color: "#25262b"
                        radius: 8
                        border.color: "#373a40"

                        ListView {
                            id: subList
                            anchors { fill: parent; margins: 8 }
                            model: root.subscriptions
                            spacing: 4
                            clip: true

                            delegate: Rectangle {
                                width: subList.width
                                height: 44
                                radius: 6
                                color: "#1e1f22"

                                RowLayout {
                                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                                    spacing: 8

                                    Text {
                                        text: modelData.name || modelData.pubkey.substring(0, 20) + "..."
                                        color: "#c1c2c5"
                                        font.pixelSize: 13
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.pubkey.substring(0, 12) + "..."
                                        color: "#5c5f66"
                                        font.pixelSize: 11
                                    }

                                    Button {
                                        text: "Remove"
                                        onClicked: {
                                            confirmPubkey = modelData.pubkey
                                            confirmName   = modelData.name || modelData.pubkey.substring(0, 12) + "…"
                                            unsubConfirm.open()
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color: parent.hovered ? "#5c1a1a" : "transparent"
                                            border.color: "#f03e3e"
                                        }
                                        contentItem: Text {
                                            text: parent.text
                                            color: "#ffa8a8"
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: root.subscriptions.length === 0
                                text: "No subscriptions yet"
                                color: "#5c5f66"
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                // ── RSS section ───────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: "RSS Bridge"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: rssContent.implicitHeight + 24
                        color: "#25262b"
                        radius: 8
                        border.color: "#373a40"

                        ColumnLayout {
                            id: rssContent
                            anchors { fill: parent; margins: 16 }
                            spacing: 10

                            // Status indicator
                            RowLayout {
                                spacing: 6
                                Text { text: "Status:"; color: "#909296"; font.pixelSize: 13 }
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: (typeof backend !== "undefined" && backend.rssRunning) ? "#40c057" : "#f03e3e"
                                }
                                Text {
                                    text: (typeof backend !== "undefined" && backend.rssRunning)
                                        ? "Running on port " + backend.rssPort
                                        : "Not running"
                                    color: "#c1c2c5"
                                    font.pixelSize: 13
                                }
                            }

                            // Feed URL rows (only when running)
                            Repeater {
                                model: (typeof backend !== "undefined" && backend.rssRunning) ? [
                                    { label: "My posts feed",     suffix: "/my/feed.xml"   },
                                    { label: "Subscriptions feed", suffix: "/feed.xml"      },
                                    { label: "OPML export",        suffix: "/opml"          }
                                ] : []

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Text {
                                        text: modelData.label + ":"
                                        color: "#909296"
                                        font.pixelSize: 11
                                        Layout.preferredWidth: 130
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 26
                                        radius: 4
                                        color: "#1e1f22"
                                        border.color: "#373a40"
                                        clip: true

                                        Text {
                                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                            verticalAlignment: Text.AlignVCenter
                                            text: "http://" + backend.rssBindAddress + ":" + backend.rssPort + modelData.suffix
                                            color: "#c1c2c5"
                                            font.family: "monospace"
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Button {
                                        text: "Copy"
                                        implicitWidth: 48
                                        implicitHeight: 26
                                        onClicked: {
                                            rssCopyHelper.text = "http://" + backend.rssBindAddress
                                                + ":" + backend.rssPort + modelData.suffix
                                            rssCopyHelper.selectAll()
                                            rssCopyHelper.copy()
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color: parent.hovered ? "#2a2d32" : "#25262b"
                                            border.color: "#373a40"
                                        }
                                        contentItem: Text {
                                            text: parent.text
                                            color: "#c1c2c5"
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                            }

                            // Hidden helper for clipboard copy
                            TextEdit {
                                id: rssCopyHelper
                                visible: false
                            }
                        }
                    }
                }

                // ── OPML section ──────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: "OPML — Subscription Import / Export"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }

                    Rectangle {
                        Layout.fillWidth: true
                        height: opmlContent.implicitHeight + 24
                        color: "#25262b"
                        radius: 8
                        border.color: "#373a40"

                        ColumnLayout {
                            id: opmlContent
                            anchors { fill: parent; margins: 16 }
                            spacing: 8

                            Text {
                                text: "Export your subscriptions as an OPML file to use in other RSS readers, " +
                                      "or import an OPML file to subscribe to all listed feeds at once."
                                color: "#909296"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                spacing: 8

                                Button {
                                    text: "Export OPML…"
                                    onClicked: opmlExportDialog.open()
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#2c5282" : "#1e3a5f"
                                        border.color: "#4dabf7"
                                    }
                                    contentItem: Text {
                                        text: parent.text; color: "#4dabf7"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }

                                Button {
                                    text: "Import OPML…"
                                    onClicked: opmlImportDialog.open()
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#2a2d32" : "#25262b"
                                        border.color: "#373a40"
                                    }
                                    contentItem: Text {
                                        text: parent.text; color: "#c1c2c5"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }

                            // Import result feedback
                            Text {
                                id: opmlFeedback
                                visible: text !== ""
                                color: "#40c057"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                // Spacer
                Item { height: 24 }
            }
        }
    }

    // ── Properties for confirmation dialog ────────────────────────────────
    property string confirmPubkey: ""
    property string confirmName:   ""

    // Subscribe dialog
    SubscribeDialog {
        id: subscribeDialog
        onSubscribed: (pubkey, name) => {
            backend.subscribe(pubkey, name)
            root.refreshSubs()
        }
    }

    // Unsubscribe confirmation dialog
    Dialog {
        id: unsubConfirm
        title: "Unsubscribe"
        modal: true
        anchors.centerIn: parent
        width: 320

        background: Rectangle {
            color: "#25262b"
            radius: 10
            border.color: "#373a40"
        }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "Remove subscription to\n\"" + root.confirmName + "\"?"
                color: "#c1c2c5"
                font.pixelSize: 14
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "This will also delete all cached posts from this author."
                color: "#909296"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                onClicked: unsubConfirm.reject()
                background: Rectangle { radius: 4; color: parent.hovered ? "#2a2d32" : "#25262b"; border.color: "#373a40" }
                contentItem: Text { text: parent.text; color: "#c1c2c5"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
            }
            Button {
                text: "Remove"
                onClicked: {
                    backend.unsubscribe(root.confirmPubkey)
                    root.refreshSubs()
                    unsubConfirm.accept()
                }
                background: Rectangle { radius: 4; color: parent.hovered ? "#5c1a1a" : "#3d1212"; border.color: "#f03e3e" }
                contentItem: Text { text: parent.text; color: "#ffa8a8"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
            }
        }
    }

    // OPML export file dialog
    FileDialog {
        id: opmlExportDialog
        title: "Export Subscriptions as OPML"
        fileMode: FileDialog.SaveFile
        nameFilters: ["OPML files (*.opml)", "All files (*)"]
        defaultSuffix: "opml"
        onAccepted: {
            const ok = backend.exportOpmlToFile(selectedFile.toString())
            opmlFeedback.color = ok ? "#40c057" : "#f03e3e"
            opmlFeedback.text  = ok ? "Exported successfully." : "Export failed."
        }
    }

    // OPML import file dialog
    FileDialog {
        id: opmlImportDialog
        title: "Import OPML Subscriptions"
        fileMode: FileDialog.OpenFile
        nameFilters: ["OPML files (*.opml *.xml)", "All files (*)"]
        onAccepted: {
            const ok = backend.importOpmlFromFile(selectedFile.toString())
            opmlFeedback.color = ok ? "#40c057" : "#f03e3e"
            opmlFeedback.text  = ok ? "Subscriptions imported." : "Import failed — check file format."
            if (ok) root.refreshSubs()
        }
    }
}
