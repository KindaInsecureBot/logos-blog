# Logos Blog — Data Model

## Namespaces (kv_module)

All data stored in kv_module with namespaced keys.

### `blog:posts` — Own Published Posts

```json
{
  "id": "uuid-v4",
  "title": "My First Post",
  "body": "# Hello\n\nThis is **markdown** content.",
  "summary": "Optional short description",
  "tags": ["logos", "p2p"],
  "created_at": "2026-03-14T06:46:00Z",
  "updated_at": "2026-03-14T06:46:00Z",
  "published": true,
  "signature": "hex-encoded-ed25519-sig"
}
```

Key format: `blog:posts:<uuid>`

### `blog:drafts` — Unpublished Drafts

Same schema as posts but `published: false`. Not broadcast over Waku.

Key format: `blog:drafts:<uuid>`

### `blog:feed:<pubkey>` — Cached Posts from Subscriptions

```json
{
  "id": "uuid-v4",
  "author_pubkey": "hex-pubkey",
  "author_name": "Display Name",
  "title": "Their Post Title",
  "body": "Markdown content",
  "summary": "Short description",
  "tags": ["topic"],
  "created_at": "2026-03-14T06:46:00Z",
  "received_at": "2026-03-14T06:47:00Z",
  "signature": "hex-sig",
  "verified": true
}
```

Key format: `blog:feed:<author-pubkey>:<post-uuid>`

### `blog:subscriptions` — Followed Authors

```json
{
  "pubkey": "hex-pubkey",
  "name": "Display Name",
  "topic": "/logos-blog/1/<pubkey>/json",
  "subscribed_at": "2026-03-14T06:46:00Z",
  "last_seen": "2026-03-14T06:50:00Z"
}
```

Key format: `blog:subscriptions:<pubkey>`

### `blog:identity` — Own Identity

```json
{
  "pubkey": "hex-pubkey",
  "privkey_encrypted": "encrypted-hex",
  "display_name": "Martin",
  "bio": "Building things at Logos",
  "created_at": "2026-03-14T06:46:00Z"
}
```

Key format: `blog:identity` (single key)

## Waku Message Schema

### Content Topic Format

Per Logos messaging spec: `/logos-blog/1/<author-pubkey>/json`

- App name: `logos-blog`
- Version: `1`
- Channel: author's public key (hex)
- Encoding: `json`

### Message Payload

```json
{
  "version": 1,
  "type": "post",
  "post": {
    "id": "uuid-v4",
    "title": "Post Title",
    "body": "Markdown content",
    "summary": "Short desc",
    "tags": ["tag1"],
    "created_at": "2026-03-14T06:46:00Z",
    "updated_at": "2026-03-14T06:46:00Z"
  },
  "author": {
    "pubkey": "hex-pubkey",
    "name": "Display Name"
  },
  "signature": "hex-ed25519-sig-of-post-json"
}
```

### Message Types

- `post` — new blog post or update to existing post
- `delete` — tombstone for a deleted post (`{ "type": "delete", "post_id": "uuid" }`)
- `profile` — author profile update (`{ "type": "profile", "name": "...", "bio": "..." }`)

## Atom Feed Schema

Generated from stored posts. Standard Atom 1.0.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Martin's Blog</title>
  <subtitle>Building things at Logos</subtitle>
  <id>urn:logos-blog:<pubkey></id>
  <link href="http://localhost:8484/@<pubkey>/feed.xml" rel="self"/>
  <updated>2026-03-14T06:46:00Z</updated>
  <author>
    <name>Martin</name>
  </author>
  <entry>
    <title>My First Post</title>
    <id>urn:logos-blog:<pubkey>:<post-uuid></id>
    <published>2026-03-14T06:46:00Z</published>
    <updated>2026-03-14T06:46:00Z</updated>
    <summary>Short description</summary>
    <content type="html">&lt;p&gt;Rendered HTML from markdown&lt;/p&gt;</content>
    <category term="logos"/>
    <category term="p2p"/>
  </entry>
</feed>
```
