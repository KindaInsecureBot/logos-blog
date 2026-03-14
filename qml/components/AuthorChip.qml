import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property string pubkey: ""
    property string name: ""

    signal clicked(string pubkey)

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 28

    Rectangle {
        anchors.fill: parent
        radius: 14
        color: hovered ? "#2a2d32" : "transparent"

        property bool hovered: false
        HoverHandler { onHoveredChanged: parent.hovered = hovered }

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                width: 20
                height: 20
                radius: 10
                color: "#2c4a6e"

                Text {
                    anchors.centerIn: parent
                    text: root.name ? root.name[0].toUpperCase() : "?"
                    color: "#4dabf7"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            Text {
                text: root.name || (root.pubkey ? root.pubkey.substring(0, 10) + "..." : "Unknown")
                color: "#c1c2c5"
                font.pixelSize: 12
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked(root.pubkey)
        }
    }
}
