import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
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

                // ── Identity section ──────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text { text: "Identity"; color: "#4dabf7"; font.pixelSize: 13; font.bold: true }

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
                                            backend.unsubscribe(modelData.pubkey)
                                            root.refreshSubs()
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
                            spacing: 8

                            RowLayout {
                                Text {
                                    text: "Status: "
                                    color: "#909296"
                                    font.pixelSize: 13
                                }
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

                            Text {
                                visible: typeof backend !== "undefined" && backend.rssRunning
                                text: "Feeds available at:\n" +
                                      "  http://localhost:" + backend.rssPort + "/my/feed.xml (own posts)\n" +
                                      "  http://localhost:" + backend.rssPort + "/feed.xml (subscriptions)"
                                color: "#5c5f66"
                                font.pixelSize: 11
                                font.family: "monospace"
                                lineHeight: 1.4
                            }
                        }
                    }
                }

                // Spacer
                Item { height: 24 }
            }
        }
    }

    // Subscribe dialog
    SubscribeDialog {
        id: subscribeDialog
        onSubscribed: (pubkey, name) => {
            backend.subscribe(pubkey, name)
            root.refreshSubs()
        }
    }
}
