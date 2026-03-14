# Logos Blog — Overview

## What Is It

A decentralized personal blogging and feed reader app for Logos (Basecamp). Publish blog posts over Waku, subscribe to others' feeds, read everything in one place. Fully P2P — no servers, no accounts, no platform.

## Key Principles

1. **P2P native** — posts are Waku messages, feeds are Waku content topics
2. **RSS compatible** — all feeds are valid RSS/Atom XML, servable via local HTTP
3. **No lock-in** — any RSS reader can consume the feeds; any Waku client can publish
4. **Offline-first** — kv_module stores everything locally, Waku syncs when available
5. **Censorship resistant** — no central authority can remove posts or ban users

## Architecture

```
logos-app (Basecamp)
  └── blog_module          ← headless plugin (Qt Core + QtRO)
  │     ├── PostStore      ← CRUD for own posts (kv_module)
  │     ├── FeedStore      ← cached posts from subscriptions (kv_module)
  │     ├── WakuSync       ← publish/subscribe via delivery_module
  │     └── RssServer      ← local HTTP server serving RSS/Atom XML
  └── blog_ui (IComponent) ← Qt Quick UI plugin
        ├── Editor         ← markdown editor for writing posts
        ├── FeedView       ← aggregated timeline of subscribed feeds
        ├── BlogView       ← single author's posts
        └── Settings       ← manage subscriptions, identity, RSS endpoint
```

## User Flows

### Publishing
1. User writes a post in the markdown editor
2. Post is saved to kv_module (own namespace)
3. Post is published as a signed Waku message to the user's content topic
4. Post is available via local RSS endpoint

### Reading
1. User subscribes to another user's content topic (by pubkey or topic URL)
2. delivery_module receives new posts on subscribed topics
3. Posts are cached in kv_module (feed namespace)
4. FeedView shows aggregated timeline

### RSS Bridge
- `localhost:8484/feed.xml` — aggregated feed of all subscriptions
- `localhost:8484/@<pubkey>/feed.xml` — single author's feed
- `localhost:8484/my/feed.xml` — your own published posts as RSS
- Standard RSS/Atom XML — works with any reader

## Tech Stack

- **Language:** C++ (Qt 6)
- **UI:** QML (Qt Quick)
- **Storage:** kv_module (Logos)
- **Networking:** delivery_module (Waku via Logos)
- **Build:** CMake + Nix flake
- **Post format:** Markdown (rendered to HTML in UI)
- **Feed format:** Atom 1.0 XML
