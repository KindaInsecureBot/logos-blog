import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    width: 44
    height: 44

    property string icon: ""
    property string tooltip: ""
    property bool   active: false

    signal clicked()

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: active ? "#2c4a6e"
             : hovered ? "#2a2d32"
             : "transparent"

        Text {
            anchors.centerIn: parent
            text: root.icon
            color: root.active ? "#4dabf7" : "#c1c2c5"
            font.pixelSize: 16
            font.bold: root.active
        }

        property bool hovered: false
        HoverHandler { onHoveredChanged: parent.hovered = hovered }
    }

    ToolTip.visible: hoverArea.containsMouse && root.tooltip !== ""
    ToolTip.text: root.tooltip
    ToolTip.delay: 500

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
        cursorShape: Qt.PointingHandCursor
    }
}
