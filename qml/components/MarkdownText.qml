import QtQuick 2.15
import QtQuick.Controls 2.15

// Minimal markdown renderer using TextEdit in read-only mode.
// Phase 6 replaces with WebEngineView + CommonMark rendering.
ScrollView {
    id: root
    property string markdownSource: ""

    clip: true

    TextEdit {
        id: textEdit
        width: root.width
        text: root.markdownSource
        color: "#c1c2c5"
        font.pixelSize: 14
        font.family: "monospace"
        wrapMode: TextEdit.Wrap
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText

    }
}
