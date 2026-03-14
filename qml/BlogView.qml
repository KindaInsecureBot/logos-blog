import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "blog"

    property string authorPubkey: ""
    property string authorName:   ""

    signal openPost(string postId, string authorPubkey)

    property var posts: []

    function refresh() {
        if (!root.authorPubkey) return
        const json = backend.getFeedByAuthor(root.authorPubkey)
        root.posts = json ? JSON.parse(json) : []
    }

    Component.onCompleted: refresh()

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "← Back"
                    onClicked: { if (StackView.view) StackView.view.pop() }
                    background: Item {}
                    contentItem: Text { text: parent.text; color: "#4dabf7"; font.pixelSize: 13 }
                }

                Item { Layout.fillWidth: true }

                AuthorChip {
                    pubkey: root.authorPubkey
                    name:   root.authorName
                }
            }

            Text {
                text: root.authorName || root.authorPubkey.substring(0, 20) + "..."
                color: "#c1c2c5"
                font.pixelSize: 20
                font.bold: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: blogList
                    model: root.posts
                    spacing: 8

                    delegate: PostCard {
                        width: blogList.width
                        postId:       modelData.id           || ""
                        title:        modelData.title        || "(Untitled)"
                        summary:      modelData.summary      || ""
                        authorPubkey: root.authorPubkey
                        authorName:   root.authorName
                        createdAt:    modelData.created_at   || ""
                        tags:         modelData.tags         || []

                        onClicked: (id, pk) => root.openPost(id, pk)
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.posts.length === 0
                        text: "No posts from this author yet."
                        color: "#5c5f66"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
