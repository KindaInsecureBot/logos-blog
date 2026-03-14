import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Item {
    id: root
    readonly property string viewId: "feed"

    signal openPost(string postId, string authorPubkey)
    signal openAuthorProfile(string pubkey)

    property var    posts:      []
    property string filterTag:  ""
    property string searchText: ""

    // Computed filtered list
    property var filteredPosts: {
        let src = root.posts
        if (root.filterTag !== "") {
            src = src.filter(p => p.tags && p.tags.indexOf(root.filterTag) >= 0)
        }
        if (root.searchText !== "") {
            const q = root.searchText.toLowerCase()
            src = src.filter(p =>
                (p.title  || "").toLowerCase().includes(q) ||
                (p.body   || "").toLowerCase().includes(q) ||
                (p.summary|| "").toLowerCase().includes(q) ||
                (p.tags   || []).some(t => t.toLowerCase().includes(q))
            )
        }
        return src
    }

    function refresh() {
        const json = backend.getAggregatedFeed()
        root.posts = json ? JSON.parse(json) : []
    }

    function applyTagFilter(tag) {
        root.filterTag  = (root.filterTag === tag) ? "" : tag
        root.searchText = ""
    }

    function clearFilters() {
        root.filterTag  = ""
        root.searchText = ""
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
            root.posts = root.posts.filter(
                p => !(p.id === postId && p.author_pubkey === authorPubkey))
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#1a1b1e"

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 12

            // ── Header row ────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Feed"
                    color: "#c1c2c5"
                    font.pixelSize: 20
                    font.bold: true
                }

                // Active filter chip
                Rectangle {
                    visible: root.filterTag !== ""
                    height: 24
                    width: filterTagLabel.implicitWidth + 28
                    radius: 12
                    color: "#1e3a5f"
                    border.color: "#4dabf7"

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Text {
                            id: filterTagLabel
                            text: root.filterTag
                            color: "#4dabf7"
                            font.pixelSize: 11
                        }
                        Text {
                            text: "✕"
                            color: "#4dabf7"
                            font.pixelSize: 10
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.filterTag = ""
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Search bar
                Rectangle {
                    Layout.preferredWidth: 220
                    height: 32
                    radius: 6
                    color: searchInput.activeFocus ? "#1e2a3a" : "#25262b"
                    border.color: searchInput.activeFocus ? "#4dabf7" : "#373a40"

                    RowLayout {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        spacing: 4

                        Text { text: "⌕"; color: "#5c5f66"; font.pixelSize: 14 }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            text: root.searchText
                            onTextChanged: root.searchText = text
                            color: "#c1c2c5"
                            font.pixelSize: 13
                            clip: true

                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                visible: searchInput.text === ""
                                text: "Search posts…"
                                color: "#5c5f66"
                                font.pixelSize: 13
                            }
                        }

                        Text {
                            visible: searchInput.text !== ""
                            text: "✕"
                            color: "#5c5f66"
                            font.pixelSize: 11
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { searchInput.text = ""; root.searchText = "" }
                            }
                        }
                    }
                }

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

            // ── Result count when filtering ───────────────────────────────
            Text {
                visible: root.filterTag !== "" || root.searchText !== ""
                text: root.filteredPosts.length + " result" +
                      (root.filteredPosts.length !== 1 ? "s" : "") +
                      (root.filterTag !== "" ? " tagged \"" + root.filterTag + "\"" : "") +
                      (root.searchText !== "" ? " matching \"" + root.searchText + "\"" : "")
                color: "#5c5f66"
                font.pixelSize: 12
                Layout.fillWidth: true
            }

            // ── Post list ─────────────────────────────────────────────────
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: feedList
                    model: root.filteredPosts
                    spacing: 8

                    delegate: PostCard {
                        width: feedList.width
                        postId:       modelData.id            || ""
                        title:        modelData.title         || "(Untitled)"
                        summary:      modelData.summary       || ""
                        authorPubkey: modelData.author_pubkey || ""
                        authorName:   modelData.author_name   || ""
                        createdAt:    modelData.created_at    || ""
                        tags:         modelData.tags          || []

                        onClicked:       (id, pk) => root.openPost(id, pk)
                        onTagClicked:    (tag)    => root.applyTagFilter(tag)
                        onAuthorClicked: (pk)     => root.openAuthorProfile(pk)
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: root.filteredPosts.length === 0
                        text: (root.filterTag !== "" || root.searchText !== "")
                            ? "No posts match your filter."
                            : "No posts in your feed.\nGo to Settings to subscribe to authors."
                        color: "#5c5f66"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
