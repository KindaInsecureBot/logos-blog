import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: parent ? parent.width : 600
    height: cardContent.implicitHeight + 24

    property string postId
    property string title
    property string summary
    property string authorPubkey
    property string authorName
    property string createdAt
    property var    tags: []

    signal clicked(string postId, string authorPubkey)
    signal tagClicked(string tag)
    signal authorClicked(string pubkey)

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 8
        color: hovered ? "#2a2d32" : "#25262b"
        border.color: "#373a40"
        border.width: 1

        property bool hovered: false
        HoverHandler { onHoveredChanged: parent.hovered = hovered }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked(root.postId, root.authorPubkey)
        }

        ColumnLayout {
            id: cardContent
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: 16
            }
            spacing: 6

            // Author + date row
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: root.authorName || (root.authorPubkey ? root.authorPubkey.substring(0,12) + "..." : "Unknown")
                    color: "#4dabf7"
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.authorClicked(root.authorPubkey)
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.createdAt ? root.createdAt.substring(0, 10) : ""
                    color: "#5c5f66"
                    font.pixelSize: 11
                }
            }

            // Title
            Text {
                text: root.title
                color: "#c1c2c5"
                font.pixelSize: 15
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            // Summary
            Text {
                visible: root.summary !== ""
                text: root.summary
                color: "#909296"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            // Tags
            Flow {
                visible: root.tags && root.tags.length > 0
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: root.tags
                    delegate: TagChip {
                        tagText: modelData
                        onClicked: root.tagClicked(tagText)
                    }
                }
            }
        }
    }
}
