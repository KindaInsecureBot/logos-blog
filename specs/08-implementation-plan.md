# Logos Blog — Implementation Plan

## Overview

Six phases, each shippable independently. Each phase builds on the previous.
Tests described are manual integration tests against a running logos-app instance.

---

## Phase 1 — Headless Core: PostStore + kv_module

**Goal:** `BlogPlugin` loads in logos_host, persists own posts to kv_module, survives restart.

### Deliverables

1. `CMakeLists.txt` with `BUILD_MODULE=ON` target (`blog_module_plugin.so`)
2. `BlogPlugin` skeleton — `PluginInterface`, `initLogos`, `setDataDir` call
3. `PostStore` — full CRUD (createDraft, update, publish, remove, listPosts, listDrafts)
4. `metadata.json` declaring `kv_module` dependency
5. `Makefile` `build-module` and `install-module` targets
6. `flake.nix` `blog-module` package output

### Key Implementation Notes

- `BlogPlugin::initLogos` must call `logosAPI = api` (base class field) first
- Call `setDataDir` on kv_module after getting the client:
  ```cpp
  kvClient->invokeRemoteMethod("kv_module", "setDataDir",
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/blog-data");
  ```
- `PostStore::generateUuid()` uses `QUuid::createUuid().toString(QUuid::WithoutBraces)`
- kv_module namespace is `"blog"` (passed as `ns` arg to all kv calls)
- `listPosts` calls `kvClient->invokeRemoteMethod("kv_module", "listAll", "blog")` then filters for keys matching `blog:posts:*` and parses each value as JSON

### Testing Phase 1

```bash
make install-module
cd ~/logos-workspace && nix run '.#logos-app-poc'
# In logos-app developer console or via another module test harness:
# → createPost("Test Title", "# Hello", "summary", ["tag1"])
# → listPosts()  — verify post appears
# → restart logos-app
# → listPosts()  — verify post persists (confirms FileBackend is active)
```

---

## Phase 2 — Basic UI: Editor + MyPostsView

**Goal:** User can write a post, save it, and see it in a list. All local, no Waku yet.

### Deliverables

1. `CMakeLists.txt` `BUILD_UI_PLUGIN=ON` target (`blog_ui.so`)
2. `BlogUIComponent` — `createWidget`, `destroyWidget`
3. `BlogBackend` — connects to `blog_module` via `ModuleProxy`, exposes post CRUD to QML
4. QML: `Main.qml`, `Sidebar`, `EditorView.qml`, `MyPostsView.qml`, `PostView.qml`, `DraftsView.qml`
5. QML resources embedded in `blog_ui.qrc` with `CMAKE_AUTORCC ON`
6. `ui_metadata.json` declaring `blog_module` dependency
7. `Makefile` `build-ui` and `install-ui` targets

### Key Implementation Notes

- Guard `Q_PLUGIN_METADATA` in `BlogBackend` with `#ifndef BLOG_UI_BUILD` (only `BlogUIComponent` has it)
- `quickWidget->setSource(QUrl("qrc:/blog_ui/Main.qml"))` — must match `.qrc` prefix exactly
- `backend` QML context property set before `setSource`:
  ```cpp
  quickWidget->rootContext()->setContextProperty("backend", backend);
  quickWidget->setSource(QUrl("qrc:/blog_ui/Main.qml"));
  ```
- `EditorView` auto-save timer: 30-second `Timer`, creates draft on first save, updates on subsequent
- `BlogBackend` methods return JSON strings; QML calls `JSON.parse()`:
  ```qml
  posts = JSON.parse(backend.listPosts())
  ```

### Testing Phase 2

```bash
make install
cd ~/logos-workspace && nix run '.#logos-app-poc'
# → Click Logos Blog in sidebar
# → Write a post in EditorView
# → Click "Save Draft" — verify draft appears in DraftsView
# → Click "Publish" — verify post appears in MyPostsView
# → Click post → PostView renders markdown
# → Delete post — verify removed from MyPostsView
# → Restart — verify published posts persist, drafts persist
```

---

## Phase 3 — WakuSync: Publish + Subscribe

**Goal:** Posts published over Waku. User can subscribe to another author and receive their posts in real-time.

### Deliverables

1. `WakuSync` class — full implementation
2. `BlogPlugin::initLogos` starts delivery_module node, subscribes to own topic
3. `BlogPlugin::publishPost` signs post and calls `WakuSync::publishPost`
4. `BlogPlugin::subscribe` calls `WakuSync::subscribeToAuthor`
5. `FeedStore` — ingestPost, ingestDelete, ingestProfile (with signature verification)
6. Ed25519 signing (via libsodium or similar; see note below)
7. `BlogPlugin::generateKeypair`, `setIdentity`, `getIdentity`
8. kv identity stored in `blog:identity`

### Key Implementation Notes

**Ed25519 library choice:**
- Option A: bundle `libsodium` (recommended — well-tested, small)
- Option B: use Qt's `QCA` (QCryptographicAlgorithm) with Ed25519 plugin
- Option C: raw OpenSSL via `EVP_DigestSign` with `EVP_PKEY_ED25519`

Whichever is chosen, wrap in a thin `src/crypto.h`:
```cpp
namespace Crypto {
    // Returns {pubkeyHex, privkeyHex} — privkey is 64-byte expanded form
    QPair<QString,QString> generateEd25519Keypair();
    QString sign(const QByteArray& privkeyBytes, const QByteArray& message);  // returns hex
    bool    verify(const QByteArray& pubkeyBytes, const QString& sigHex,
                   const QByteArray& message);
}
```

**delivery_module init sequence:**
```cpp
void WakuSync::start() {
    m_delivery->invokeRemoteMethod("delivery_module", "createNode",
        QString(R"({"logLevel":"WARN","mode":"Core","preset":"logos.dev"})"));
    m_delivery->invokeRemoteMethod("delivery_module", "start");

    // Subscribe to own topic
    subscribeToAuthor(m_ownPubkey);

    // Re-subscribe to all known subscriptions (loaded from FeedStore)
    for (const QString& pubkey : m_feedStore->subscribedPubkeys()) {
        subscribeToAuthor(pubkey);
    }
}
```

**Signature input:** Canonical JSON (keys sorted, no whitespace) of the envelope minus `signature` field. Use `QJsonDocument(obj).toJson(QJsonDocument::Compact)` then sort keys via `QJsonObject` insertion order — note: Qt sorts QJsonObject keys alphabetically in Compact mode since Qt 6.

**Incoming message routing in WakuSync:**
```cpp
// registered in start()
m_api->on("delivery_module", "messageReceived", [this](QVariantList args) {
    QString topic   = args.value(1).toString();
    QString payload = QByteArray::fromBase64(args.value(2).toString().toUtf8());
    emit messageReceived(topic, payload);
});
```

`BlogPlugin` connects `WakuSync::messageReceived` to `FeedStore::ingestPost` and emits `postReceived` on success.

### Testing Phase 3

Run two logos-app instances in separate home directories (or two VMs).

```bash
# Instance A: get pubkey
# → Settings → copy pubkey: a3f8...

# Instance B: subscribe
# → Settings → Subscribe → paste a3f8...

# Instance A: publish a post
# → EditorView → Publish

# Instance B: verify
# → FeedView shows the post from Instance A (within ~5s)
# → Restart Instance B — post still in FeedView (persisted in kv_module)
```

---

## Phase 4 — Feed Aggregation + FeedView

**Goal:** Aggregated timeline of all subscriptions, sorted by `created_at` descending. Profile updates applied.

### Deliverables

1. `FeedStore::getAggregatedFeed()` — merges all `blog:feed:<pubkey>:*` keys, sorts by `created_at` desc
2. `FeedView.qml` — paginated `ListView` (page size 50), pull-to-refresh
3. `BlogView.qml` — single-author view, filters `FeedStore` by pubkey
4. `SubscribeDialog.qml` — subscribe by hex pubkey
5. `SettingsView.qml` subscription list (list, view, remove)
6. `BlogBackend` signals: `postReceived`, `postDeleted`, `profileUpdated` wired to QML `Connections`
7. History retrieval on subscribe (`WakuSync::requestHistory` with last 30 days)

### Key Implementation Notes

**Aggregated feed pagination** — `FeedStore::getAggregatedFeed` loads all posts into memory and sorts. For Phase 4 this is acceptable (typical user: <5 authors × <100 posts = <500 items). Phase 6 adds cursor-based pagination.

**FeedView refresh strategy:**
- On `backend.postReceived` signal: prepend new post to model (no full refresh)
- On `backend.postDeleted` signal: remove matching item from model by id
- Manual refresh button: calls `backend.getAggregatedFeed()` and rebuilds model

**Author display name:** Posts in `FeedStore` carry `author_name` copied from the envelope at ingest time. Profile updates (`ingestProfile`) update `blog:profiles:<pubkey>` and emit `profileUpdated`. FeedView re-renders affected `PostCard` items.

### Testing Phase 4

```bash
# Subscribe to 2–3 authors (can reuse Phase 3 setup or mock with stored posts)
# → FeedView shows all authors' posts merged
# → Sorted: newest first
# → Click author chip → BlogView shows only that author's posts
# → Author publishes new post → FeedView prepends it without full refresh
# → Author deletes post → FeedView removes it
# → Author updates profile → AuthorChip shows new name
```

---

## Phase 5 — RSS Bridge

**Goal:** `RssServer` running on `localhost:8484`. Any RSS reader can subscribe to the generated feeds.

### Deliverables

1. `RssServer` — full implementation (see spec 05-rss-bridge.md)
2. `BlogPlugin` starts `RssServer` in `initLogos` using settings from kv_module
3. OPML export and import endpoints
4. `SettingsView.qml` RSS section — port, bind address, status indicator, copy-URL buttons
5. `BlogBackend` `rssRunning`, `rssPort`, `rssBindAddress` properties wired to `RssServer`

### Key Implementation Notes

**RssServer lifecycle:** Started in `BlogPlugin::initLogos` after kv_module is available. If port is in use, try port+1 up to 5 times, then emit `errorOccurred`. The UI shows the actual port from `backend.rssPort`.

**Markdown → HTML in RSS:** MVP regex-based (see spec 05). Full CommonMark via `cmark` library is Phase 6.

**ETag/304 flow:** Feed readers that send `If-None-Match` get a 304 if content hasn't changed, saving bandwidth for automated polling.

**OPML round-trip test:** Export from Instance A, import on Instance B — all subscriptions should transfer.

### Testing Phase 5

```bash
make install
cd ~/logos-workspace && nix run '.#logos-app-poc'

# Basic feed check
curl http://localhost:8484/health           # → 200 OK
curl http://localhost:8484/my/feed.xml      # → valid Atom XML with own posts
curl http://localhost:8484/feed.xml         # → aggregated feed of subscriptions

# Validate Atom XML
xmllint --noout http://localhost:8484/my/feed.xml

# Load in an RSS reader
# → Add http://localhost:8484/feed.xml to Newsboat/NetNewsWire/etc.
# → Verify titles, summaries, HTML content render correctly

# ETag / 304
ETAG=$(curl -sI http://localhost:8484/my/feed.xml | grep -i etag | tr -d '\r' | cut -d' ' -f2)
curl -H "If-None-Match: $ETAG" -I http://localhost:8484/my/feed.xml  # → HTTP/1.1 304

# OPML
curl http://localhost:8484/opml -o subs.opml
curl -X POST http://localhost:8484/opml -d @subs.opml  # → {"imported": N}
```

---

## Phase 6 — Polish

**Goal:** Production-quality UX. Search, tags, OPML, full markdown, profile pages.

### Deliverables

**Search:**
- `BlogPlugin::searchPosts(query)` — scans all `blog:posts:*` and `blog:feed:*:*` keys for title/body/tag matches
- `BlogBackend::searchPosts(query)` — exposed to QML
- `FeedView` search bar (shown on focus / keyboard shortcut)

**Tag filtering:**
- `BlogPlugin::getPostsByTag(tag)` — filters aggregated feed
- `TagChip` in FeedView is clickable; clicking filters to that tag

**Full CommonMark rendering:**
- Add `cmark` (GitHub's C CommonMark library) as a CMake dependency
- Replace `RssServer::markdownToHtml` regex impl with `cmark_markdown_to_html`
- Reuse in `PostView.qml` via `MarkdownText` component (WebEngineView or `QQuickTextDocument`)

**Profile pages:**
- `ProfileView.qml` — shows author name, bio, pubkey, post count, post list
- Accessible from `AuthorChip` long-press or secondary tap

**Rate limiting:**
- `FeedStore` per-author rate limiter (100 msgs/60s window)
- Max post body size check (512 KB)
- Max posts per author cap (1000 posts)

**Registry topic (optional):**
- Subscribe to `/logos-blog/1/registry/json`
- Authors can announce themselves; FeedView gets a "Discover" tab

**OPML improvements:**
- Import from file picker (QML `FileDialog`)
- Export to file picker

### Testing Phase 6

- Publish 20+ posts with various tags; verify tag filter works
- Search for a word appearing in a post body; verify result appears
- Import 10-subscription OPML file; verify all 10 appear in SubscriptionList
- Send 200 rapid messages from a test pubkey; verify rate limiter drops excess
- CommonMark: publish post with tables, code blocks, nested lists; verify renders correctly in PostView and RSS feed
