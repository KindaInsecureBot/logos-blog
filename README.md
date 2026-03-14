# Logos Blog

A decentralized P2P blogging plugin for [Logos](https://logos.co/) (Basecamp). Write, publish, and read blog posts over the Waku network — no server required.

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
        ├── PostStore   ← own posts/drafts via kv_module
        ├── FeedStore   ← cached subscription posts + rate limiting
        ├── WakuSync    ← Waku publish/subscribe via delivery_module
        ├── RssServer   ← local HTTP server on 127.0.0.1:8484
        └── Crypto      ← Ed25519 sign/verify (OpenSSL EVP)
```

## Features

- **Write & publish** — Markdown editor with draft auto-save (30s), publish to Waku P2P network
- **Subscribe & follow** — Subscribe to authors by Ed25519 public key, receive posts in real-time
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

## Data Model

All data lives in `kv_module` under the `"blog"` namespace:

| Key pattern | Content |
|-------------|---------|
| `posts:<uuid>` | Published post JSON (with Ed25519 signature) |
| `drafts:<uuid>` | Unpublished draft JSON |
| `feed:<pubkey>:<post-uuid>` | Cached post from a subscription |
| `subscriptions:<pubkey>` | Subscription metadata (name, topic, last_seen) |
| `profiles:<pubkey>` | Author profile (name, bio, avatar_url) |
| `identity` | Own keypair and display name |
| `settings:rss_port` | RSS server port (default: 8484) |
| `settings:rss_bind` | RSS server bind address (default: 127.0.0.1) |

## Waku Protocol

Posts are published to content topics of the form `/logos-blog/1/<author-pubkey>/json`.

Each message is a JSON envelope:

```json
{
  "version": 1,
  "type": "post",
  "author": { "pubkey": "<hex>", "name": "Display Name" },
  "timestamp": "2026-03-14T10:00:00Z",
  "post": { "id": "<uuid>", "title": "…", "body": "…", "tags": [] },
  "signature": "<ed25519-sig-hex>"
}
```

Message types: `post`, `delete`, `profile`. Signatures are verified before any content is stored.

## Usage

1. Launch `logos-app` with the plugin installed
2. Click **Logos Blog** in the sidebar
3. **Settings** → copy your public key and share it so others can subscribe to you
4. **New Post** → write in Markdown, save draft, then publish
5. **Settings → Subscribe** → paste another author's public key
6. **Feed** → see their posts as they arrive over Waku

### RSS Reader Integration

Add `http://localhost:8484/feed.xml` to any Atom-compatible RSS reader (Newsboat, NetNewsWire, Reeder, etc.) to read your subscriptions outside of Logos.

To export subscriptions: Settings → **Export OPML…**
To import subscriptions: Settings → **Import OPML…**

## Screenshots

<!-- TODO: add screenshots -->

## License

Part of the Logos project. See the root repository for license information.
