import QtQuick 2.15

Item {
    id: root
    property string tagText: ""
    signal clicked()

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 22

    Rectangle {
        anchors.fill: parent
        radius: 11
        color: hovered ? "#2c4a6e" : "#1e3a5f"
        border.color: "#4dabf7"
        border.width: 1

        property bool hovered: false
        HoverHandler { onHoveredChanged: parent.hovered = hovered }

        Text {
            id: label
            anchors.centerIn: parent
            text: root.tagText
            color: "#4dabf7"
            font.pixelSize: 11
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }
    }
}
