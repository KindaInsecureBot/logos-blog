import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "myposts"

    signal openPost(string postId)
    signal newPost()

    property var posts: []

    function refresh() {
        const json = backend.listPosts()
        root.posts = json ? JSON.parse(json) : []
    }

    Component.onCompleted: refresh()

    Connections {
        target: typeof backend !== "undefined" ? backend : null
        function onPostPublished() { root.refresh() }
        function onPostDeleted()   { root.refresh() }
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
                    text: "My Posts"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "+ New Post"
                    onClicked: root.newPost()
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

            // Post list
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: listView
                    model: root.posts
                    spacing: 8

                    delegate: Item {
                        width: listView.width
                        height: postCard.height

                        readonly property var post: root.posts[index]

                        PostCard {
                            id: postCard
                            width: parent.width
                            postId:      post.id        || ""
                            title:       post.title     || "(Untitled)"
                            summary:     post.summary   || ""
                            authorName:  backend.displayName || "Me"
                            createdAt:   post.created_at || ""
                            tags:        post.tags || []

                            onClicked: (id, pk) => root.openPost(id)
                        }

                        // Delete button (hover reveal)
                        Button {
                            anchors {
                                right: parent.right
                                top: parent.top
                                margins: 12
                            }
                            text: "Delete"
                            visible: parent.hovered || deleteHover
                            property bool deleteHover: false

                            onClicked: {
                                backend.deletePost(post.id)
                                root.refresh()
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? "#5c1a1a" : "#3d1515"
                                border.color: "#f03e3e"
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "#ffa8a8"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                            }

                            HoverHandler { onHoveredChanged: parent.deleteHover = hovered }
                        }

                        property bool hovered: false
                        HoverHandler { onHoveredChanged: parent.hovered = hovered }
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: root.posts.length === 0
                        text: "No published posts yet.\nClick '+ New Post' to get started."
                        color: "#5c5f66"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
