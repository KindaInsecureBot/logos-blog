import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "feed"

    signal openPost(string postId, string authorPubkey)

    property var posts: []

    function refresh() {
        const json = backend.getAggregatedFeed()
        root.posts = json ? JSON.parse(json) : []
    }

    Component.onCompleted: refresh()

    Connections {
        target: typeof backend !== "undefined" ? backend : null
        function onPostReceived(postJson) {
            try {
                const post = JSON.parse(postJson)
                root.posts.unshift(post)
                postsChanged()
            } catch(e) { root.refresh() }
        }
        function onPostDeleted(postId, authorPubkey) {
            root.posts = root.posts.filter(p => !(p.id === postId && p.author_pubkey === authorPubkey))
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
            spacing: 16

            // Header
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Feed"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Refresh"
                    onClicked: root.refresh()
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
            }

            // Post list
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: feedList
                    model: root.posts
                    spacing: 8

                    delegate: PostCard {
                        width: feedList.width
                        postId:       modelData.id           || ""
                        title:        modelData.title        || "(Untitled)"
                        summary:      modelData.summary      || ""
                        authorPubkey: modelData.author_pubkey || ""
                        authorName:   modelData.author_name  || ""
                        createdAt:    modelData.created_at   || ""
                        tags:         modelData.tags         || []

                        onClicked: (id, pk) => root.openPost(id, pk)
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: root.posts.length === 0
                        text: "No posts in your feed.\nGo to Settings to subscribe to authors."
                        color: "#5c5f66"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
