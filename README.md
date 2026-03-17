# Logos Blog

A decentralized P2P blogging plugin for [Logos](https://logos.co/) (Basecamp). Write, publish, and read blog posts over the Logos network — no server required.

## Architecture

Two build targets:

| Target | Role | Process |
|--------|------|---------|
| `blog_module` | Headless backend plugin | `logos_host` |
| `blog_ui` | IComponent UI plugin | `logos-app` |

```
logos-app
  └── blog_ui.so        ← QML/C++ UI (FeedView, EditorView, SettingsView…)
        └── BlogBackend ← QObject bridge via QtRemoteObjects
              └── [ModuleProxy → blog_module]

logos_host
  └── blog_module_plugin.so
        ├── PostStore    ← own posts/drafts via kv_module
        ├── FeedStore    ← cached subscription posts + rate limiting
        ├── ChatSync     ← P2P broadcast via Chat SDK (chatsdk_module)
        ├── StorageSync  ← content-addressed storage via storage_module
        ├── RssServer    ← local HTTP server on 127.0.0.1:8484
        └── Crypto       ← Ed25519 sign/verify (OpenSSL EVP)

lez-registry/                ← on-chain author→CID index (SPEL Rust workspace)
  ├── blog_registry_core/  ← shared types + in-memory mock
  └── methods/guest/       ← SPEL entry point (compiled for LEZ)
                               register_post / remove_post / get_posts
                               cap: 10 000 CIDs per author (oldest evicted)
```

### Module dependencies

| Module | Interface | Purpose |
|--------|-----------|---------|
| `kv_module` | `org.logos.KVModuleInterface` | Local index and metadata (drafts, subscriptions, settings) |
| `storage_module` | `org.logos.StorageModuleInterface` | Content-addressed storage for post bodies (uploadUrl / downloadChunks) |
| `chatsdk_module` | `org.logos.ChatSDKModuleInterface` | P2P broadcast of post CIDs via sendMessage / joinConversation |

## Publish / receive flow

### Publishing a post

```
User (QML)
  → BlogBackend::publishPost(id)
  → BlogPlugin::publishPost(id)
  → PostStore::publish(id)
      stores metadata in kv_module, emits postPublished(id, postJson)
  → BlogPlugin handler:
      1. StorageSync::uploadContent(postJson) → CID
      2. kv_module.set("blog", "cid:<id>", cid)          ← local CID index
      3. buildSignedEnvelope("post", {post, cid})         ← Ed25519-signed
      4. ChatSync::sendPost(signedEnvelope)
          → chatsdk_module::sendMessage(convoId, hexPayload)
          → Logos Chat SDK broadcasts on "logos-blog:<pubkey>" conversation
```

### Receiving a post (subscriber)

```
Chat SDK fires incoming message event
  → BlogPlugin::connectChatModule() listener
      Subscription gate: only process from subscribed authors + own convo
  → ChatSync::onChatMessage(convoId, hexPayload)
  → BlogPlugin slot:
      1. Decode hex → JSON envelope
      2. Extract CID from envelope
      3. StorageSync::downloadContent(cid) → full post JSON
      4. Merge post body back into envelope
      5. FeedStore::ingestPost(envelope)
          - Verify Ed25519 signature
          - Apply rate limiting (100 msgs/60s per author)
          - Store to kv_module under feed:<pubkey>:<postId>
```

### On-chain index (LEZ Registry)

After uploading to storage, the blog plugin can optionally register the CID
on the LEZ ledger via the `lez-registry` SPEL program:

```
StorageSync::uploadContent() → CID
  → lez_registry::register_post(author_pubkey, cid)     ← on-chain
```

Any subscriber can call `get_posts(author_pubkey)` to enumerate all known CIDs
for an author and bootstrap their feed from cold start without relying on
Chat SDK history replay.

## Features

- **Write & publish** — Markdown editor with draft auto-save (30s), publish to Chat SDK + Storage
- **Subscribe & follow** — Subscribe to authors by Ed25519 public key, receive post CIDs in real-time
- **Content-addressed storage** — Post bodies stored in `storage_module`; CIDs shared over Chat SDK
- **On-chain registry** — `lez-registry` SPEL program indexes `author → [CID]` on LEZ
- **Aggregated feed** — Timeline of all subscriptions sorted by date
- **Search** — Full-text search across own posts and subscribed feeds
- **Tag filtering** — Click any tag chip to filter the feed
- **Author profiles** — View author name, bio, pubkey, and post history
- **RSS Bridge** — Atom 1.0 feeds served at `http://localhost:8484/` for any RSS reader
- **OPML** — Export/import subscription lists for portability between readers
- **Ed25519 signatures** — All published content is cryptographically signed; FeedStore verifies before accepting
- **Rate limiting** — 100 messages/60s per author, 512 KB body cap, 1000 posts/author cap

## RSS Endpoints

When the plugin is running, the following HTTP endpoints are available:

| Path | Description |
|------|-------------|
| `GET /` | HTML index with links to all feeds |
| `GET /health` | `{"status":"ok"}` — liveness check |
| `GET /my/feed.xml` | Atom 1.0 feed of your own published posts |
| `GET /feed.xml` | Atom 1.0 aggregated feed of all subscriptions |
| `GET /@<pubkey>/feed.xml` | Atom 1.0 feed for a specific author |
| `GET /opml` | OPML 2.0 export of all subscriptions |
| `POST /opml` | Import OPML body — subscribes to all listed feeds |

All feed endpoints support `ETag` / `If-None-Match` for efficient conditional GETs (304 Not Modified).

### Quick test

```bash
curl http://localhost:8484/health
curl http://localhost:8484/my/feed.xml
curl http://localhost:8484/opml -o subs.opml
curl -X POST http://localhost:8484/opml --data-binary @subs.opml
```

## Build

### Prerequisites

- [Nix](https://nixos.org/) with flakes enabled, **or** CMake 3.21+ with Qt 6.2+ and OpenSSL
- `logos-cpp-sdk` and `logos-liblogos` merged prefixes (provided by Nix)

### With Nix (recommended)

```bash
# Build and install both targets
make install

# Build headless module only
make build-module
make install-module

# Build UI plugin only
make build-ui
make install-ui
```

### With CMake directly

```bash
cmake -B build \
  -DBUILD_MODULE=ON \
  -DBUILD_UI_PLUGIN=ON \
  -DLOGOS_CPP_SDK_ROOT=/path/to/logos-cpp-sdk \
  -DLOGOS_LIBLOGOS_ROOT=/path/to/logos-liblogos
cmake --build build
```

### LEZ Registry (Rust)

```bash
cd lez-registry

# Local tests (no SPEL runtime needed)
cargo test

# On-chain build (requires SPEL toolchain)
cargo build --release --features on-chain
```

## Data Model

### kv_module ("blog" namespace)

| Key pattern | Content |
|-------------|---------|
| `posts:<uuid>` | Published post metadata JSON (title, tags, signature; no body) |
| `cid:<uuid>` | Storage module CID for the post body |
| `drafts:<uuid>` | Unpublished draft JSON (full body, local only) |
| `feed:<pubkey>:<post-uuid>` | Cached post from a subscription |
| `subscriptions:<pubkey>` | Subscription metadata (name, convoId, last_seen) |
| `profiles:<pubkey>` | Author profile (name, bio, avatar_url) |
| `identity` | Own keypair and display name |
| `settings:rss_port` | RSS server port (default: 8484) |
| `settings:rss_bind` | RSS server bind address (default: 127.0.0.1) |

### storage_module

Published post bodies are uploaded as JSON blobs. Each post has a CID (content
identifier) returned by `uploadUrl()`. The CID is embedded in the Chat SDK
envelope and stored locally in kv under `cid:<uuid>`.

### Chat SDK protocol

Posts are broadcast on conversations identified by `"logos-blog:<author-pubkey>"`.
Each message payload is a hex-encoded, Ed25519-signed JSON envelope:

```json
{
  "version": 1,
  "type": "post",
  "author": { "pubkey": "<hex>", "name": "Display Name" },
  "timestamp": "2026-03-14T10:00:00Z",
  "cid": "<storage-module-cid>",
  "post": { "id": "<uuid>", "title": "…", "tags": [] },
  "signature": "<ed25519-sig-hex>"
}
```

Message types: `post` (has `cid` field), `delete` (has `post_id`), `profile`.
Signatures are verified before any content is stored. Subscribers download the
full post body from `storage_module` using the CID in the envelope.

## Usage

1. Launch `logos-app` with the plugin installed
2. Click **Logos Blog** in the sidebar
3. **Settings** → copy your public key and share it so others can subscribe to you
4. **New Post** → write in Markdown, save draft, then publish
5. **Settings → Subscribe** → paste another author's public key
6. **Feed** → see their posts as they arrive via Chat SDK + Storage

### RSS Reader Integration

Add `http://localhost:8484/feed.xml` to any Atom-compatible RSS reader (Newsboat, NetNewsWire, Reeder, etc.) to read your subscriptions outside of Logos.

To export subscriptions: Settings → **Export OPML…**
To import subscriptions: Settings → **Import OPML…**

## Screenshots

<!-- TODO: add screenshots -->

## License

Part of the Logos project. See the root repository for license information.
