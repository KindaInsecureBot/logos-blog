import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "profile"

    property string authorPubkey: ""
    property var    posts:        []

    // Profile data populated from profileMap or backend
    property string authorName: ""
    property string authorBio:  ""

    signal openPost(string postId, string authorPubkey)

    function refresh() {
        if (!authorPubkey) return
        const json = backend.getFeedByAuthor(authorPubkey)
        const arr  = json ? JSON.parse(json) : []
        root.posts = arr

        // Try to pull name/bio from the first post's author_name field
        if (arr.length > 0 && arr[0].author_name) {
            root.authorName = arr[0].author_name
        } else if (!root.authorName) {
            root.authorName = authorPubkey.substring(0, 16) + "…"
        }
    }

    Component.onCompleted: refresh()

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            // ── Back button + title ───────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "← Back"
                    onClicked: stackView.pop()
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? "#2a2d32" : "transparent"
                        border.color: "#373a40"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "#c1c2c5"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Text {
                    text: "Author Profile"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }
            }

            // ── Profile card ──────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: profileContent.implicitHeight + 24
                color: "#25262b"
                radius: 10
                border.color: "#373a40"

                ColumnLayout {
                    id: profileContent
                    anchors { fill: parent; margins: 20 }
                    spacing: 10

                    // Avatar placeholder + name row
                    RowLayout {
                        spacing: 14

                        Rectangle {
                            width: 48; height: 48; radius: 24
                            color: "#1e3a5f"
                            border.color: "#4dabf7"

                            Text {
                                anchors.centerIn: parent
                                text: root.authorName ? root.authorName[0].toUpperCase() : "?"
                                color: "#4dabf7"
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            spacing: 4

                            Text {
                                text: root.authorName || "Unknown Author"
                                color: "#c1c2c5"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Text {
                                visible: root.authorBio !== ""
                                text: root.authorBio
                                color: "#909296"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Pubkey row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Pubkey:"
                            color: "#5c5f66"
                            font.pixelSize: 12
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 26
                            radius: 4
                            color: "#1e1f22"
                            border.color: "#373a40"
                            clip: true

                            Text {
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                verticalAlignment: Text.AlignVCenter
                                text: root.authorPubkey
                                color: "#c1c2c5"
                                font.family: "monospace"
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                        }

                        Button {
                            text: "Copy"
                            implicitWidth: 48
                            implicitHeight: 26
                            onClicked: {
                                pkCopyHelper.text = root.authorPubkey
                                pkCopyHelper.selectAll()
                                pkCopyHelper.copy()
                            }
                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? "#2a2d32" : "#25262b"
                                border.color: "#373a40"
                            }
                            contentItem: Text {
                                text: parent.text; color: "#c1c2c5"
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    TextEdit { id: pkCopyHelper; visible: false }

                    // Stats row
                    RowLayout {
                        spacing: 24

                        ColumnLayout {
                            spacing: 2
                            Text { text: root.posts.length.toString(); color: "#4dabf7"; font.pixelSize: 18; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "Posts"; color: "#5c5f66"; font.pixelSize: 11 }
                        }
                    }
                }
            }

            // ── Posts section ─────────────────────────────────────────────
            Text {
                text: "Posts"
                color: "#4dabf7"
                font.pixelSize: 14
                font.bold: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: postList
                    model: root.posts
                    spacing: 8

                    delegate: PostCard {
                        width: postList.width
                        postId:       modelData.id            || ""
                        title:        modelData.title         || "(Untitled)"
                        summary:      modelData.summary       || ""
                        authorPubkey: modelData.author_pubkey || root.authorPubkey
                        authorName:   modelData.author_name   || root.authorName
                        createdAt:    modelData.created_at    || ""
                        tags:         modelData.tags          || []

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

    // Listen for profile updates from backend
    Connections {
        target: typeof backend !== "undefined" ? backend : null
        function onProfileUpdated(pubkey, profileJson) {
            if (pubkey !== root.authorPubkey) return
            try {
                const profile = JSON.parse(profileJson)
                if (profile.name) root.authorName = profile.name
                if (profile.bio)  root.authorBio  = profile.bio
            } catch(e) {}
        }
    }
}
