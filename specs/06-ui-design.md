# Logos Blog — UI Design

## Navigation Structure

```
Main.qml
├── Sidebar (56px, vertical icon bar)
│   ├── [feed icon]      → FeedView
│   ├── [edit icon]      → EditorView (new post)
│   ├── [posts icon]     → MyPostsView
│   ├── [drafts icon]    → DraftsView
│   └── [settings icon]  → SettingsView
│
└── StackView (fills remaining width)
    ├── FeedView          (default / home)
    ├── BlogView          (pushed when author clicked)
    ├── PostView          (pushed when post clicked)
    ├── EditorView        (new post or edit draft)
    ├── MyPostsView
    ├── DraftsView
    └── SettingsView
```

Navigation is driven by `Sidebar` emitting `navigate(viewId: string)`. `StackView.navigateTo(viewId)` maps view IDs to component URLs:

```qml
function navigateTo(viewId, params) {
    const map = {
        "feed":     "FeedView.qml",
        "editor":   "EditorView.qml",
        "myposts":  "MyPostsView.qml",
        "drafts":   "DraftsView.qml",
        "settings": "SettingsView.qml"
    }
    const url = Qt.resolvedUrl(map[viewId] ?? "FeedView.qml")
    stackView.push(url, params ?? {})
}
```

`BlogView` and `PostView` are always pushed onto the stack (not a direct nav target) and provide a Back button that calls `stackView.pop()`.

---

## Screen Breakdown

### 1. FeedView

Aggregated reverse-chronological timeline of all subscribed authors.

**QML file:** `FeedView.qml`

```
┌─────────────────────────────────────────────┐
│ Feed            [Refresh]  [Subscribe +]     │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────┐│
│  │ [Avatar] Martin K    @a3f8...   Mar 14  ││
│  │ My First Post                           ││
│  │ A short description of the post...      ││
│  │ [logos] [p2p]                           ││
│  └─────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────┐│
│  │ [Avatar] Alice       @b7e9...   Mar 13  ││
│  │ Building on Waku                        ││
│  │ ...                                     ││
│  └─────────────────────────────────────────┘│
│  (scroll)                                   │
└─────────────────────────────────────────────┘
```

**Key properties:**
```qml
// FeedView.qml
Item {
    id: feedView
    property string viewId: "feed"

    property var posts: []   // parsed from JSON; refresh() repopulates

    function refresh() {
        let json = backend.getAggregatedFeed()
        posts = JSON.parse(json)
    }

    // Called on postReceived signal from backend
    Connections {
        target: backend
        function onPostReceived() { feedView.refresh() }
    }

    ListView {
        id: listView
        anchors.fill: parent
        model: posts
        delegate: PostCard {
            postId:       modelData.id
            title:        modelData.title
            summary:      modelData.summary
            authorPubkey: modelData.author_pubkey
            authorName:   modelData.author_name
            createdAt:    modelData.created_at
            tags:         modelData.tags ?? []
            onClicked: stackView.push("PostView.qml", {postData: modelData})
            onAuthorClicked: stackView.push("BlogView.qml", {authorPubkey: pubkey})
        }
    }
}
```

**Header buttons:**
- **Refresh** — calls `backend.getAggregatedFeed()` and rebuilds model
- **Subscribe +** — opens `SubscribeDialog`

---

### 2. BlogView

Single author's posts. Accessible by clicking an author anywhere in the UI.

**QML file:** `BlogView.qml`

```
┌─────────────────────────────────────────────┐
│ ← Back    Martin K (@a3f8...)               │
│           Building things at Logos          │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────┐│
│  │ My First Post                  Mar 14   ││
│  │ A short description...                  ││
│  │ [logos] [p2p]                           ││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘
```

```qml
// BlogView.qml
Item {
    property string authorPubkey  // required
    property string authorName: ""
    property var posts: []

    Component.onCompleted: refresh()

    function refresh() {
        posts = JSON.parse(backend.getFeedByAuthor(authorPubkey))
        // Load profile from first post or cached profile
    }
}
```

---

### 3. PostView

Rendered single post with full HTML content.

**QML file:** `PostView.qml`

```
┌─────────────────────────────────────────────┐
│ ← Back    [Edit]  (own post only)           │
│                                             │
│ # My First Post                             │
│ Martin K · March 14, 2026                   │
├─────────────────────────────────────────────┤
│                                             │
│  <rendered markdown HTML>                   │
│                                             │
│  [logos] [p2p]                              │
│                                             │
└─────────────────────────────────────────────┘
```

```qml
// PostView.qml
Item {
    property var postData   // full post JSON object
    property bool isOwnPost: postData.author_pubkey === backend.ownPubkey

    // Rendered HTML in a read-only TextEdit or WebEngineView (Phase 6)
    // MVP: MarkdownText component (QML TextEdit with basic styling)
    MarkdownText {
        markdown: postData.body
        anchors.fill: parent
        anchors.margins: 24
    }

    // Edit button only shown for own posts
    ToolButton {
        visible: isOwnPost
        text: "Edit"
        onClicked: stackView.push("EditorView.qml", {draftId: postData.id, existingPost: postData})
    }
}
```

---

### 4. EditorView

Markdown editor with live preview. Used for both new posts and editing drafts.

**QML file:** `EditorView.qml`

```
┌─────────────────────────────────────────────┐
│ [← Cancel]  Title field           [Publish] │
│                                   [Save Draft]│
├──────────────────┬──────────────────────────┤
│                  │                          │
│  TextArea        │   Live Preview           │
│  (markdown)      │   (MarkdownText)         │
│                  │                          │
│                  │                          │
├──────────────────┴──────────────────────────┤
│ Tags: [logos ×] [p2p ×]  [+ Add tag]        │
│ Summary: __________________________________ │
└─────────────────────────────────────────────┘
```

```qml
// EditorView.qml
Item {
    property string draftId: ""           // "" = new post
    property var existingPost: null       // populate fields if editing

    property string title: ""
    property string body: ""
    property string summary: ""
    property var    tags: []

    Component.onCompleted: {
        if (existingPost) {
            title   = existingPost.title
            body    = existingPost.body
            summary = existingPost.summary
            tags    = existingPost.tags ?? []
            draftId = existingPost.id
        }
    }

    // Auto-save timer — every 30 seconds
    Timer {
        interval: 30000
        running: true
        repeat: true
        onTriggered: saveDraft()
    }

    function saveDraft() {
        if (draftId === "") {
            draftId = backend.createPost(title, body, summary, tags)
        } else {
            backend.updatePost(draftId, title, body, summary, tags)
        }
    }

    function publishPost() {
        saveDraft()
        if (backend.publishPost(draftId)) {
            stackView.pop()
        }
    }

    SplitView {
        orientation: Qt.Horizontal
        // Left: TextArea for markdown input
        // Right: MarkdownText for live preview
    }
}
```

**Interaction details:**
- Title field: `TextField` at top, single line
- Body: `ScrollView { TextArea { ... } }` — monospace font, line numbers optional
- Live preview: `MarkdownText` bound to `body` property, updates on every keystroke
- Tags: horizontal `Repeater` of `TagChip` components + inline `TextField` to add new tags
- **Publish** button calls `saveDraft()` then `backend.publishPost(draftId)`
- **Save Draft** button calls `saveDraft()` explicitly (also auto-saves every 30s)
- **Cancel** prompts if unsaved changes exist (check dirty flag), then pops

---

### 5. MyPostsView

Own published posts, sorted by `created_at` descending.

**QML file:** `MyPostsView.qml`

```
┌─────────────────────────────────────────────┐
│ My Posts              [New Post]            │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────┐│
│  │ My First Post         Mar 14  [Edit][Del]││
│  │ A short description...                  ││
│  │ [logos] [p2p]                           ││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘
```

Each row has **Edit** (pushes `EditorView`) and **Delete** (confirmation dialog, then `backend.deletePost`).

---

### 6. DraftsView

Unpublished drafts. Includes auto-saved drafts from EditorView.

**QML file:** `DraftsView.qml`

```
┌─────────────────────────────────────────────┐
│ Drafts                                      │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────┐│
│  │ Untitled Draft        (unsaved) [Edit][×]││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘
```

---

### 7. SettingsView

Three sections: Identity, Subscriptions, RSS Endpoint.

**QML file:** `SettingsView.qml`

```
┌─────────────────────────────────────────────┐
│ Settings                                    │
├─────────────────────────────────────────────┤
│ IDENTITY                                    │
│ Pubkey:  a3f8e1d2...  [Copy]               │
│ Name:    [Martin K________________]         │
│ Bio:     [Building things at Logos_________]│
│          [Save Identity]                    │
│                                             │
│ SUBSCRIPTIONS                               │
│ ┌──────────────────────────────────────┐   │
│ │ Martin K   @a3f8...  [View] [Remove] │   │
│ │ Alice      @b7e9...  [View] [Remove] │   │
│ └──────────────────────────────────────┘   │
│ [+ Subscribe to Author]                     │
│ [Export OPML] [Import OPML]                 │
│                                             │
│ RSS ENDPOINT                                │
│ Status:  ● Running on localhost:8484        │
│ Port:    [8484____]                         │
│ Bind:    [127.0.0.1___________]             │
│          [Apply]                            │
│ /feed.xml     [Copy URL]                    │
│ /my/feed.xml  [Copy URL]                    │
└─────────────────────────────────────────────┘
```

```qml
// SettingsView.qml
Item {
    property string viewId: "settings"

    // Identity section
    Column {
        TextField { id: nameField;  text: backend.displayName }
        TextArea  { id: bioField;   text: backend.bio }
        Button {
            text: "Save Identity"
            onClicked: backend.setIdentity(nameField.text, bioField.text)
        }
    }

    // RSS status indicator
    Row {
        Rectangle {
            width: 8; height: 8; radius: 4
            color: backend.rssRunning ? "#44cc44" : "#cc4444"
        }
        Text {
            text: backend.rssRunning
                ? "Running on " + backend.rssBindAddress + ":" + backend.rssPort
                : "Stopped"
        }
    }
}
```

---

### 8. SubscribeDialog

Modal dialog for adding a new subscription.

**QML file:** `SubscribeDialog.qml`

```
┌─────────────────────────────────┐
│ Subscribe to Author             │
│                                 │
│ Public Key (hex, 64 chars):     │
│ [________________________________]│
│                                 │
│ Display Name (optional):        │
│ [________________________]      │
│                                 │
│ [Cancel]              [Subscribe]│
└─────────────────────────────────┘
```

Validates that pubkey is exactly 64 hex characters before enabling Subscribe button:
```qml
Button {
    text: "Subscribe"
    enabled: /^[0-9a-f]{64}$/i.test(pubkeyField.text)
    onClicked: {
        backend.subscribe(pubkeyField.text, nameField.text)
        close()
    }
}
```

---

## QML Component Hierarchy (Full)

```
Main.qml
├── Sidebar
│   └── SidebarButton (×5)
├── StackView
│   ├── FeedView
│   │   ├── ToolBar (Refresh, Subscribe+)
│   │   ├── ListView
│   │   │   └── PostCard (delegate)
│   │   │       ├── AuthorChip
│   │   │       └── TagChip (×N)
│   │   └── SubscribeDialog (Loader, on demand)
│   ├── BlogView
│   │   ├── AuthorHeader
│   │   └── ListView → PostCard
│   ├── PostView
│   │   ├── ToolBar (Back, Edit)
│   │   └── MarkdownText
│   ├── EditorView
│   │   ├── ToolBar (Cancel, Save Draft, Publish)
│   │   ├── TextField (title)
│   │   ├── SplitView
│   │   │   ├── TextArea (markdown input)
│   │   │   └── MarkdownText (preview)
│   │   ├── TagEditor
│   │   │   ├── Repeater → TagChip
│   │   │   └── TextField (add tag)
│   │   └── TextField (summary)
│   ├── MyPostsView
│   │   └── ListView → PostCard (with Edit/Delete actions)
│   ├── DraftsView
│   │   └── ListView → DraftCard
│   └── SettingsView
│       ├── IdentitySection
│       ├── SubscriptionList
│       │   └── Repeater → SubscriptionRow
│       └── RssSection
└── ErrorBanner (anchored to top, z: 100)
```

---

## Layout and Responsive Design

### Base Dimensions

- Minimum window width: 640px
- Minimum window height: 480px
- Sidebar: fixed 56px wide
- Content area: fills remaining width

### Responsive Breakpoints

The UI targets desktop (Basecamp is a desktop app). Mobile layout is not a primary concern but the layout avoids fixed pixel widths in the content area.

```qml
// PostCard.qml — fills available width
Item {
    width: ListView.view ? ListView.view.width : parent.width
    height: contentColumn.implicitHeight + 24
}
```

### Split View (EditorView)

The editor/preview split defaults to 50/50. The split handle is draggable. On narrow windows (< 800px), the preview pane is hidden and accessible via a toggle button.

```qml
SplitView {
    handle: Rectangle { width: 1; color: "#e0e0e0" }

    TextArea {
        SplitView.preferredWidth: parent.width * 0.5
        SplitView.minimumWidth: 200
    }
    MarkdownText {
        SplitView.fillWidth: true
        visible: parent.width > 400   // hide preview on narrow splits
    }
}
```

### Theme

Inherits the Logos/Basecamp application theme. Components use:
- `palette.text`, `palette.base`, `palette.highlight` for colors
- System font family at 14px base size
- Consistent 8px spacing grid (margins/padding in multiples of 8)
