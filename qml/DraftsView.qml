import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    readonly property string viewId: "drafts"

    signal editDraft(string draftId)

    property var drafts: []

    function refresh() {
        const json = backend.listDrafts()
        root.drafts = json ? JSON.parse(json) : []
    }

    Component.onCompleted: refresh()

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors {
                fill: parent
                margins: 24
            }
            spacing: 16

            // Header
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Drafts"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.drafts.length + " draft" + (root.drafts.length !== 1 ? "s" : "")
                    color: "#5c5f66"
                    font.pixelSize: 13
                }
            }

            // Draft list
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: listView
                    model: root.drafts
                    spacing: 8

                    delegate: Rectangle {
                        width: listView.width
                        height: row.implicitHeight + 24
                        radius: 8
                        color: hoverArea.containsMouse ? "#2a2d32" : "#25262b"
                        border.color: "#373a40"

                        RowLayout {
                            id: row
                            anchors {
                                fill: parent
                                margins: 16
                            }
                            spacing: 12

                            Column {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: modelData.title || "(Untitled)"
                                    color: "#c1c2c5"
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                    width: parent.width
                                }

                                Text {
                                    visible: modelData.summary !== ""
                                    text: modelData.summary || ""
                                    color: "#909296"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    width: parent.width
                                }

                                Text {
                                    text: "Last updated: " + (modelData.updated_at || "").substring(0, 10)
                                    color: "#5c5f66"
                                    font.pixelSize: 11
                                }
                            }

                            Column {
                                spacing: 6

                                Button {
                                    text: "Edit"
                                    onClicked: root.editDraft(modelData.id)
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

                                Button {
                                    text: "Publish"
                                    onClicked: {
                                        backend.publishPost(modelData.id)
                                        root.refresh()
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#1a4a1a" : "#0d2e0d"
                                        border.color: "#40c057"
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#40c057"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }

                                Button {
                                    text: "Delete"
                                    onClicked: {
                                        backend.deletePost(modelData.id)
                                        root.refresh()
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? "#5c1a1a" : "#3d1515"
                                        border.color: "#f03e3e"
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: "#ffa8a8"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            // Don't capture click — let buttons handle it
                        }
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: root.drafts.length === 0
                        text: "No drafts.\nStart writing and it will auto-save here."
                        color: "#5c5f66"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
