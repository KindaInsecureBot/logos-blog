import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: root
    title: "Subscribe to Author"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel

    signal subscribed(string pubkey, string displayName)

    onAccepted: {
        const pk = pubkeyField.text.trim()
        if (pk !== "") {
            root.subscribed(pk, nameField.text.trim())
            pubkeyField.text = ""
            nameField.text   = ""
        }
    }

    background: Rectangle {
        color: "#25262b"
        radius: 8
        border.color: "#373a40"
        border.width: 1
    }

    header: Item {
        height: 48
        Text {
            anchors.centerIn: parent
            text: root.title
            color: "#c1c2c5"
            font.pixelSize: 15
            font.bold: true
        }
    }

    ColumnLayout {
        width: 380
        spacing: 12

        Text {
            text: "Author's public key (hex)"
            color: "#909296"
            font.pixelSize: 12
        }

        TextField {
            id: pubkeyField
            Layout.fillWidth: true
            placeholderText: "e.g. a3f8c2d1..."
            color: "#c1c2c5"
            placeholderTextColor: "#5c5f66"
            font.family: "monospace"
            font.pixelSize: 12
            background: Rectangle {
                radius: 4
                color: "#1e1f22"
                border.color: parent.activeFocus ? "#4dabf7" : "#373a40"
            }
            leftPadding: 10
        }

        Text {
            text: "Display name (optional)"
            color: "#909296"
            font.pixelSize: 12
        }

        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "e.g. Alice"
            color: "#c1c2c5"
            placeholderTextColor: "#5c5f66"
            font.pixelSize: 13
            background: Rectangle {
                radius: 4
                color: "#1e1f22"
                border.color: parent.activeFocus ? "#4dabf7" : "#373a40"
            }
            leftPadding: 10
        }
    }
}
