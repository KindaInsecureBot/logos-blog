import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "post"

    property string postId: ""
    property string authorPubkey: ""

    property var currentPost: null

    function loadPost() {
        if (!root.postId) return
        let json
        if (root.authorPubkey) {
            // Feed post
            json = backend.getFeedByAuthor(root.authorPubkey)
            if (json) {
                const all = JSON.parse(json)
                const match = all.find(p => p.id === root.postId)
                if (match) { root.currentPost = match; return }
            }
        }
        // Own post
        json = backend.getPost(root.postId)
        if (json) root.currentPost = JSON.parse(json)
    }

    Component.onCompleted: loadPost()

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors {
                fill: parent
                margins: 24
            }
            spacing: 16

            // Back button
            Button {
                text: "← Back"
                onClicked: {
                    if (StackView.view) StackView.view.pop()
                }
                background: Item {}
                contentItem: Text {
                    text: parent.text
                    color: "#4dabf7"
                    font.pixelSize: 13
                }
            }

            // Post content
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                visible: root.currentPost !== null

                ColumnLayout {
                    width: Math.min(parent.width, 720)
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 16

                    // Title
                    Text {
                        text: root.currentPost ? root.currentPost.title || "" : ""
                        Layout.fillWidth: true
                        color: "#c1c2c5"
                        font.pixelSize: 28
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    // Meta row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        AuthorChip {
                            pubkey: root.currentPost ? root.currentPost.author_pubkey || "" : ""
                            name:   root.currentPost ? root.currentPost.author_name || backend.displayName || "" : ""
                        }

                        Text {
                            text: root.currentPost ? (root.currentPost.created_at || "").substring(0, 10) : ""
                            color: "#5c5f66"
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // Tags
                    Flow {
                        visible: root.currentPost && root.currentPost.tags && root.currentPost.tags.length > 0
                        Layout.fillWidth: true
                        spacing: 4

                        Repeater {
                            model: root.currentPost ? root.currentPost.tags || [] : []
                            delegate: TagChip { tagText: modelData }
                        }
                    }

                    // Summary
                    Text {
                        visible: root.currentPost && root.currentPost.summary
                        text: root.currentPost ? root.currentPost.summary || "" : ""
                        Layout.fillWidth: true
                        color: "#909296"
                        font.pixelSize: 14
                        font.italic: true
                        wrapMode: Text.Wrap
                    }

                    // Separator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#373a40"
                    }

                    // Body — markdown rendered as plain text (Phase 6: CommonMark)
                    MarkdownText {
                        Layout.fillWidth: true
                        implicitHeight: 400
                        markdownSource: root.currentPost ? root.currentPost.body || "" : ""
                    }
                }
            }

            // Loading / error state
            Text {
                visible: root.currentPost === null
                anchors.centerIn: parent
                text: "Post not found"
                color: "#5c5f66"
                font.pixelSize: 14
            }
        }
    }
}
