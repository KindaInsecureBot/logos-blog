import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    height: visible ? content.height + 16 : 0
    visible: false
    clip: true

    function show(msg) {
        messageText.text = msg
        visible = true
        dismissTimer.restart()
    }

    function dismiss() {
        visible = false
    }

    Timer {
        id: dismissTimer
        interval: 5000
        onTriggered: root.dismiss()
    }

    Rectangle {
        id: content
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        height: messageText.implicitHeight + 16
        radius: 6
        color: "#3d1515"
        border.color: "#f03e3e"
        border.width: 1

        Text {
            id: messageText
            anchors {
                left: parent.left
                right: closeBtn.left
                verticalCenter: parent.verticalCenter
                leftMargin: 12
                rightMargin: 8
            }
            color: "#ffa8a8"
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Text {
            id: closeBtn
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: 12
            }
            text: "✕"
            color: "#f03e3e"
            font.pixelSize: 14
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.dismiss()
            }
        }
    }

    Behavior on height {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }
}
