# Logos Blog — Waku Protocol

## Content Topics

### Naming Convention

Follows the [Logos content topic spec](https://lip.logos.co/messaging/informational/23/topics.html#content-topics):

```
/logos-blog/1/<author-pubkey>/json
```

| Segment | Value |
|---------|-------|
| app | `logos-blog` |
| version | `1` |
| channel | author's Ed25519 public key, hex-encoded (64 chars) |
| encoding | `json` |

**Example:**
```
/logos-blog/1/a3f8e1d2c4b5a6f7e8d9c0b1a2f3e4d5c6b7a8f9e0d1c2b3a4f5e6d7c8b9a0f1/json
```

Each author has exactly one content topic. All message types (post, delete, profile) are published on the same topic. Subscribers filter by `type` field in the payload.

### Topic Discovery

Subscribers learn topics via:
1. Manually entering a pubkey in the Subscribe dialog
2. Future: a registry topic `/logos-blog/1/registry/json` for author announcements (Phase 6)

---

## Message Types

All messages share a common envelope. The `type` field determines which sub-object is present.

### Common Envelope

```json
{
  "version": 1,
  "type": "<post|delete|profile>",
  "author": {
    "pubkey": "hex-ed25519-pubkey-64-chars",
    "name": "Display Name"
  },
  "timestamp": "2026-03-14T06:46:00Z",
  "signature": "hex-ed25519-signature-128-chars"
}
```

The `signature` field covers a canonical JSON string of the envelope **without** the `signature` field itself:

```
sig_input = JSON.stringify({version, type, author, timestamp, <type-specific-fields>})
            sorted keys, no extra whitespace
signature = Ed25519.sign(privkey, UTF8(sig_input)).hex()
```

### Type: `post`

New post or update to existing post. Identified by `post.id`. If `post.id` already exists in FeedStore, apply last-write-wins using `post.updated_at`.

```json
{
  "version": 1,
  "type": "post",
  "author": {
    "pubkey": "hex-pubkey",
    "name": "Martin"
  },
  "timestamp": "2026-03-14T06:46:00Z",
  "post": {
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "title": "My First Post",
    "body": "# Hello\n\nMarkdown **content**.",
    "summary": "A short introduction",
    "tags": ["logos", "p2p"],
    "created_at": "2026-03-14T06:46:00Z",
    "updated_at": "2026-03-14T06:46:00Z"
  },
  "signature": "hex-sig"
}
```

**Fields:**

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `post.id` | UUID v4 | yes | Stable identifier; same id on updates |
| `post.title` | string | yes | Max 512 chars |
| `post.body` | string | yes | Markdown; max 512 KB |
| `post.summary` | string | no | Max 512 chars; auto-truncated if omitted |
| `post.tags` | string[] | no | Max 20 tags, each max 64 chars |
| `post.created_at` | ISO 8601 | yes | Set once at creation; never changes |
| `post.updated_at` | ISO 8601 | yes | Updated on every edit |

### Type: `delete`

Tombstone for a deleted post. Recipients must remove the post from FeedStore and mark it deleted to suppress RSS appearance.

```json
{
  "version": 1,
  "type": "delete",
  "author": {
    "pubkey": "hex-pubkey",
    "name": "Martin"
  },
  "timestamp": "2026-03-14T07:00:00Z",
  "delete": {
    "post_id": "550e8400-e29b-41d4-a716-446655440000"
  },
  "signature": "hex-sig"
}
```

Recipients must verify the signature matches the author's pubkey before deleting. This prevents spoofed deletions.

### Type: `profile`

Author profile update. Recipients update the cached profile for this pubkey in `blog:profiles:<pubkey>`.

```json
{
  "version": 1,
  "type": "profile",
  "author": {
    "pubkey": "hex-pubkey",
    "name": "Martin"
  },
  "timestamp": "2026-03-14T07:05:00Z",
  "profile": {
    "name": "Martin K",
    "bio": "Building things at Logos",
    "avatar_url": ""
  },
  "signature": "hex-sig"
}
```

`avatar_url` is intentionally empty or a data URI (no external URLs in MVP). Profile updates are last-write-wins by `timestamp`.

---

## Signature Scheme — Ed25519

### Key Generation

```cpp
// In BlogPlugin::generateKeypair()
// Use libsodium or Qt's QCA, or bundled Ed25519 implementation

QByteArray seed = QRandomGenerator::securelySeeded().generate64BitBytes(32);
// seed → Ed25519 keypair
// pubkey: 32 bytes → 64-char hex string
// privkey: stored encrypted in blog:identity.privkey_encrypted
```

### Signing

```cpp
// Canonical JSON: sorted keys, no whitespace, UTF-8
QString BlogPlugin::signEnvelope(const QJsonObject& envelope) {
    // Remove "signature" key if present
    QJsonObject toSign = envelope;
    toSign.remove("signature");

    // Canonical form: QJsonDocument sorted keys
    QByteArray canonical = QJsonDocument(toSign).toJson(QJsonDocument::Compact);

    // Ed25519 sign
    QByteArray sig = ed25519_sign(m_privkeyBytes, canonical);  // 64 bytes
    return sig.toHex();
}
```

### Verification

```cpp
// In FeedStore::verifySignature()
bool FeedStore::verifySignature(const QJsonObject& envelope, const QString& pubkeyHex) {
    QJsonObject toVerify = envelope;
    QString sigHex = toVerify.take("signature").toString();
    if (sigHex.isEmpty()) return false;

    QByteArray canonical = QJsonDocument(toVerify).toJson(QJsonDocument::Compact);
    QByteArray sig    = QByteArray::fromHex(sigHex.toLatin1());
    QByteArray pubkey = QByteArray::fromHex(pubkeyHex.toLatin1());

    return ed25519_verify(pubkey, sig, canonical);
}
```

**Rejection policy:** Any message with an invalid signature is silently dropped. No error is shown to the user (spam/noise suppression). Invalid-sig events are logged at DEBUG level.

---

## Post Deduplication and Conflict Resolution

### Deduplication

A post is identified by `(author_pubkey, post.id)`. When `FeedStore::ingestPost` receives a message:

1. Look up `blog:feed:<author-pubkey>:<post-id>` in kv_module
2. If not found → store the new post
3. If found → compare `updated_at` timestamps (last-write-wins):
   - Incoming `updated_at` > stored `updated_at` → overwrite
   - Incoming `updated_at` ≤ stored `updated_at` → discard (older version)
4. Waku may deliver the same message multiple times (relay deduplication is best-effort) — the LWW rule handles duplicates naturally

```cpp
bool FeedStore::isNewer(const QJsonObject& incoming, const QJsonObject& existing) {
    QDateTime incomingTs = QDateTime::fromString(
        incoming["post"]["updated_at"].toString(), Qt::ISODate);
    QDateTime existingTs = QDateTime::fromString(
        existing["updated_at"].toString(), Qt::ISODate);
    return incomingTs > existingTs;
}
```

### Conflict Resolution

Two clients editing the same post (e.g., user on two devices) will produce two messages with the same `post.id` but different `updated_at`. The one with the later `updated_at` wins. This is acceptable for a personal blog — the author controls the private key and publishes intentionally.

**Edge case — clock skew:** If `updated_at` values differ by less than 1 second, the message with the lexicographically larger `signature` hex wins (tie-breaker). This is deterministic and avoids oscillation.

---

## Historical Message Retrieval (Waku Store Protocol)

### When It Runs

On `WakuSync::subscribeToAuthor`, after subscribing to the live topic, the client requests history for the past 30 days via the Waku store protocol:

```cpp
void WakuSync::requestHistory(const QString& pubkeyHex, const QDateTime& since) {
    QString topic = topicForPubkey(pubkeyHex);
    QJsonObject params {
        {"topic", topic},
        {"startTime", since.toSecsSinceEpoch()},
        {"endTime",   QDateTime::currentSecsSinceEpoch()}
    };
    m_delivery->invokeRemoteMethod("delivery_module", "queryHistory",
        topic, params.toString());
}
```

### Availability Caveat

Waku store protocol requires a store node to be reachable. In the `logos.dev` preset, store nodes are provided by the Logos infrastructure. If store is unavailable, history retrieval is silently skipped — the client will receive new messages from subscription time forward.

`WakuSync::start` calls `requestHistory` for all existing subscriptions (loaded from FeedStore) with `since = now - 30 days`.

---

## Rate Limiting / Spam Considerations

### Content-Level Filtering (MVP)

The only protection in Phase 1–4 is signature verification: messages without a valid Ed25519 signature are dropped. A bad actor with a valid keypair can still spam.

### Planned Mitigations (Phase 6)

1. **Per-author rate limit:** Discard messages from an author if more than 100 messages arrive in a 60-second window. Counter stored in memory (not persisted).

   ```cpp
   // In FeedStore::ingestPost, before verifySignature
   if (m_rateLimiter.tooFast(authorPubkey)) return false;
   ```

2. **Max post body size:** Reject posts with `body` > 512 KB. Applied before signature verification (cheap check first).

3. **Max posts per author:** Keep at most 1000 posts per subscribed author in kv_module. On overflow, drop the oldest `created_at`.

4. **Subscription-gated:** Only process messages from pubkeys in `blog:subscriptions`. Messages from unknown pubkeys are dropped entirely. This is the primary spam defence — users control who they subscribe to.

   ```cpp
   // WakuSync::onDeliveryMessage — extract author pubkey from topic
   QString pubkeyFromTopic = topic.split("/").value(3);
   if (!m_feedStore->isSubscribed(pubkeyFromTopic)) return;
   ```

5. **Future — Waku RLN:** Logos plans to integrate RLN (Rate Limiting Nullifiers) into delivery_module. When available, integrate transparently.
