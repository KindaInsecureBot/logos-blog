import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "editor"

    property string draftId: ""   // "" = new post
    property string title:   ""
    property string body:    ""
    property string summary: ""
    property var    tags:    []

    signal postPublished(string postId)
    signal saved(string draftId)

    // Loaded from existing draft on appear
    Component.onCompleted: {
        if (root.draftId !== "") {
            const json = backend.getPost(root.draftId)
            if (json) {
                const post = JSON.parse(json)
                titleField.text   = post.title   || ""
                bodyArea.text     = post.body    || ""
                summaryField.text = post.summary || ""
                tagsField.text    = (post.tags || []).join(", ")
            }
        }
    }

    // Auto-save timer (every 30 seconds)
    Timer {
        id: autoSaveTimer
        interval: 30000
        repeat: true
        running: bodyArea.text.length > 0 || titleField.text.length > 0
        onTriggered: saveDraft()
    }

    function saveDraft() {
        if (!titleField.text && !bodyArea.text) return
        const tagList = tagsField.text.split(",")
            .map(t => t.trim()).filter(t => t !== "")

        if (root.draftId === "") {
            root.draftId = backend.createPost(
                titleField.text, bodyArea.text, summaryField.text, tagList)
            root.saved(root.draftId)
        } else {
            backend.updatePost(root.draftId, titleField.text,
                bodyArea.text, summaryField.text, tagList)
        }
    }

    function publishNow() {
        saveDraft()
        if (root.draftId === "") return
        const ok = backend.publishPost(root.draftId)
        if (ok) {
            root.postPublished(root.draftId)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors {
                fill: parent
                margins: 24
            }
            spacing: 12

            // ── Title bar ─────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: root.draftId === "" ? "New Post" : "Edit Draft"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Save Draft"
                    onClicked: saveDraft()
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? "#2a2d32" : "#25262b"
                        border.color: "#373a40"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "#c1c2c5"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Button {
                    text: "Publish"
                    onClicked: publishNow()
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? "#2c5282" : "#1e3a5f"
                        border.color: "#4dabf7"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "#4dabf7"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // ── Title field ───────────────────────────────────────────────
            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "Post title…"
                font.pixelSize: 16
                color: "#c1c2c5"
                placeholderTextColor: "#5c5f66"
                background: Rectangle {
                    radius: 6
                    color: "#25262b"
                    border.color: titleField.activeFocus ? "#4dabf7" : "#373a40"
                }
                leftPadding: 12
            }

            // ── Summary field ─────────────────────────────────────────────
            TextField {
                id: summaryField
                Layout.fillWidth: true
                placeholderText: "Short summary (optional)…"
                font.pixelSize: 13
                color: "#c1c2c5"
                placeholderTextColor: "#5c5f66"
                background: Rectangle {
                    radius: 6
                    color: "#25262b"
                    border.color: summaryField.activeFocus ? "#4dabf7" : "#373a40"
                }
                leftPadding: 12
            }

            // ── Tags field ────────────────────────────────────────────────
            TextField {
                id: tagsField
                Layout.fillWidth: true
                placeholderText: "Tags (comma-separated)…"
                font.pixelSize: 13
                color: "#c1c2c5"
                placeholderTextColor: "#5c5f66"
                background: Rectangle {
                    radius: 6
                    color: "#25262b"
                    border.color: tagsField.activeFocus ? "#4dabf7" : "#373a40"
                }
                leftPadding: 12
            }

            // ── Editor / preview split ────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                // Markdown editor
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: "#25262b"
                    border.color: bodyArea.activeFocus ? "#4dabf7" : "#373a40"

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true

                        TextArea {
                            id: bodyArea
                            placeholderText: "Write your post in Markdown…\n\n# Heading\n\n**bold** *italic*\n\n- list item"
                            font.family: "monospace"
                            font.pixelSize: 13
                            color: "#c1c2c5"
                            placeholderTextColor: "#5c5f66"
                            wrapMode: TextArea.Wrap
                            background: null
                            padding: 12
                        }
                    }
                }

                // Live preview
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: "#1e1f22"
                    border.color: "#373a40"

                    Column {
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                            margins: 4
                        }

                        Text {
                            text: "Preview"
                            color: "#5c5f66"
                            font.pixelSize: 11
                            leftPadding: 12
                            topPadding: 8
                        }
                    }

                    MarkdownText {
                        anchors {
                            fill: parent
                            topMargin: 28
                            margins: 12
                        }
                        markdownSource: bodyArea.text
                    }
                }
            }

            // ── Status bar ────────────────────────────────────────────────
            Text {
                Layout.fillWidth: true
                text: bodyArea.text.length + " chars · " +
                      bodyArea.text.split(/\s+/).filter(w => w).length + " words"
                color: "#5c5f66"
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
