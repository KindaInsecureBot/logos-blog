# Logos Blog — Components

## Overview

Two build targets:
- `blog_module` — headless plugin (Qt Core + Qml + RemoteObjects only, runs in logos_host)
- `blog_ui` — IComponent UI plugin (Qt Quick + Widgets, runs in logos-app)

---

## blog_module — Headless Plugin

### BlogPlugin

Entry point. Implements `PluginInterface`. Owns all sub-components.

**File:** `src/blog_plugin.h`

```cpp
#pragma once
#include "plugin_interface.h"
#include "post_store.h"
#include "feed_store.h"
#include "waku_sync.h"
#include "rss_server.h"
#include <QtPlugin>

class BlogPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit BlogPlugin(QObject* parent = nullptr);

    Q_INVOKABLE QString version() const override { return "0.1.0"; }
    Q_INVOKABLE void initLogos(LogosAPI* api) override;

    // Identity
    Q_INVOKABLE QString getIdentity();
    Q_INVOKABLE bool setIdentity(const QString& displayName, const QString& bio);
    Q_INVOKABLE QString generateKeypair();   // returns JSON {pubkey, privkey_encrypted}

    // Post management (delegates to PostStore)
    Q_INVOKABLE QString createPost(const QString& title, const QString& body,
                                   const QString& summary, const QStringList& tags);
    Q_INVOKABLE bool updatePost(const QString& id, const QString& title,
                                const QString& body, const QString& summary,
                                const QStringList& tags);
    Q_INVOKABLE bool publishPost(const QString& id);   // saves draft → post, broadcasts via Waku
    Q_INVOKABLE bool deletePost(const QString& id);    // removes + sends tombstone
    Q_INVOKABLE QString getPost(const QString& id);    // returns JSON or ""
    Q_INVOKABLE QString listPosts();                   // returns JSON array, published only
    Q_INVOKABLE QString listDrafts();                  // returns JSON array, drafts only

    // Subscriptions (delegates to FeedStore)
    Q_INVOKABLE bool subscribe(const QString& pubkey, const QString& displayName);
    Q_INVOKABLE bool unsubscribe(const QString& pubkey);
    Q_INVOKABLE QString listSubscriptions();           // returns JSON array
    Q_INVOKABLE QString getFeedPosts(const QString& pubkey);  // "" = all subscribed authors
    Q_INVOKABLE QString getAggregatedFeed();           // all subscriptions, sorted by created_at desc

    // RSS
    Q_INVOKABLE int getRssPort();
    Q_INVOKABLE bool setRssPort(int port);
    Q_INVOKABLE QString getRssBindAddress();
    Q_INVOKABLE bool setRssBindAddress(const QString& address);  // "127.0.0.1" or "0.0.0.0"

signals:
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);       // from Waku subscription
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void subscriptionAdded(const QString& pubkey);
    void identityChanged();

private:
    LogosAPI*   m_api    = nullptr;
    PostStore*  m_posts  = nullptr;
    FeedStore*  m_feed   = nullptr;
    WakuSync*   m_waku   = nullptr;
    RssServer*  m_rss    = nullptr;

    QString m_ownPubkey;
    void loadOrCreateIdentity();
};
```

---

### PostStore

Manages own posts and drafts via kv_module. Signs posts with the local Ed25519 key.

**File:** `src/post_store.h`

```cpp
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include "module_proxy.h"  // from logos-cpp-sdk

class PostStore : public QObject {
    Q_OBJECT
public:
    explicit PostStore(QObject* parent = nullptr);

    void setKvClient(ModuleProxy* kv);
    void setPrivkey(const QByteArray& privkeyBytes);  // 32-byte Ed25519 seed

    // Returns new post ID (uuid v4), saves as draft
    QString createDraft(const QString& title, const QString& body,
                        const QString& summary, const QStringList& tags);

    // Update existing draft or post (returns false if not found)
    bool update(const QString& id, const QString& title, const QString& body,
                const QString& summary, const QStringList& tags);

    // Move draft → post, sign, return signed JSON ready for Waku broadcast
    // Sets published=true, updated_at=now, computes signature
    QString publish(const QString& id);

    // Hard-delete from kv_module; returns tombstone JSON for Waku
    QString remove(const QString& id);

    // Accessors
    QJsonObject getPost(const QString& id);
    QJsonArray  listPosts();    // published only, sorted by created_at desc
    QJsonArray  listDrafts();   // drafts only

    // Signed payload for Waku: wraps post in WakuMessage envelope
    QString buildWakuPayload(const QJsonObject& post);

signals:
    void postCreated(const QString& id);
    void postUpdated(const QString& id);
    void postPublished(const QString& id, const QString& signedJson);
    void postRemoved(const QString& id);

private:
    ModuleProxy* m_kv      = nullptr;
    QByteArray   m_privkey;

    static constexpr auto NS = "blog";

    void      savePost(const QJsonObject& post, bool draft);
    QJsonObject loadPost(const QString& id, bool draft);
    QString   signPost(const QJsonObject& post);  // returns hex Ed25519 sig
    QString   generateUuid();
};
```

**kv_module key layout:**

| Key | Value |
|-----|-------|
| `blog:posts:<uuid>` | JSON post object (published=true, has signature) |
| `blog:drafts:<uuid>` | JSON post object (published=false, no signature) |
| `blog:identity` | JSON identity object |
| `blog:settings:rss_port` | "8484" |
| `blog:settings:rss_bind` | "127.0.0.1" |

---

### FeedStore

Caches received posts from subscribed authors. Verifies Ed25519 signatures before storing.

**File:** `src/feed_store.h`

```cpp
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include "module_proxy.h"

class FeedStore : public QObject {
    Q_OBJECT
public:
    explicit FeedStore(QObject* parent = nullptr);

    void setKvClient(ModuleProxy* kv);

    // Subscriptions
    bool        subscribe(const QString& pubkey, const QString& displayName);
    bool        unsubscribe(const QString& pubkey);
    QJsonArray  listSubscriptions();
    bool        isSubscribed(const QString& pubkey) const;
    void        updateLastSeen(const QString& pubkey);

    // Ingestion — called by WakuSync on messageReceived
    // Returns true if accepted (new or updated), false if rejected (bad sig, duplicate)
    bool ingestPost(const QJsonObject& envelope);
    bool ingestDelete(const QString& authorPubkey, const QString& postId);
    bool ingestProfile(const QString& pubkey, const QJsonObject& profile);

    // Feed queries
    QJsonArray getPostsByAuthor(const QString& pubkey);
    QJsonArray getAggregatedFeed();                        // all authors, sorted by created_at desc
    QJsonObject getPost(const QString& authorPubkey, const QString& postId);

signals:
    void postIngested(const QString& authorPubkey, const QString& postId);
    void postDeleted(const QString& authorPubkey, const QString& postId);
    void profileUpdated(const QString& pubkey, const QString& displayName);

private:
    ModuleProxy* m_kv = nullptr;

    static constexpr auto NS = "blog";

    bool   verifySignature(const QJsonObject& post, const QString& pubkeyHex);
    void   savePost(const QJsonObject& post);
    void   removePost(const QString& authorPubkey, const QString& postId);
    bool   isNewer(const QJsonObject& incoming, const QJsonObject& existing);
};
```

**kv_module key layout:**

| Key | Value |
|-----|-------|
| `blog:feed:<author-pubkey>:<post-uuid>` | JSON cached post |
| `blog:subscriptions:<pubkey>` | JSON subscription record |
| `blog:profiles:<pubkey>` | JSON {pubkey, name, bio, updated_at} |

---

### WakuSync

Bridges PostStore/FeedStore to delivery_module. Handles pub/sub lifecycle.

**File:** `src/waku_sync.h`

```cpp
#pragma once
#include <QObject>
#include <QSet>
#include "module_proxy.h"

class WakuSync : public QObject {
    Q_OBJECT
public:
    explicit WakuSync(QObject* parent = nullptr);

    void setDeliveryClient(ModuleProxy* delivery);
    void setOwnPubkey(const QString& pubkeyHex);

    // Called once in BlogPlugin::initLogos after node is started
    void start();

    // Publish own post — called by BlogPlugin::publishPost
    void publishPost(const QString& signedEnvelopeJson);

    // Publish delete tombstone
    void publishDelete(const QString& postId);

    // Publish profile update
    void publishProfile(const QString& displayName, const QString& bio);

    // Subscribe to an author's topic — called by BlogPlugin::subscribe
    void subscribeToAuthor(const QString& pubkeyHex);

    // Unsubscribe from an author's topic
    void unsubscribeFromAuthor(const QString& pubkeyHex);

    // Replay history for a topic via Waku store protocol (best-effort)
    void requestHistory(const QString& pubkeyHex, const QDateTime& since);

signals:
    void messageReceived(const QString& topic, const QString& payloadJson);
    void nodeStarted();
    void deliveryError(const QString& error);

private slots:
    void onDeliveryMessage(const QVariantList& args);  // connected to delivery_module event

private:
    ModuleProxy* m_delivery  = nullptr;
    QString      m_ownPubkey;
    QSet<QString> m_subscribedTopics;

    static QString topicForPubkey(const QString& pubkeyHex);
    // → "/logos-blog/1/<pubkeyHex>/json"
};
```

---

### RssServer

Local HTTP server serving Atom 1.0 feeds. Uses `QTcpServer` — no external HTTP library.

**File:** `src/rss_server.h`

```cpp
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "post_store.h"
#include "feed_store.h"

class RssServer : public QObject {
    Q_OBJECT
public:
    explicit RssServer(QObject* parent = nullptr);

    void setPostStore(PostStore* posts);
    void setFeedStore(FeedStore* feed);

    bool start(const QString& bindAddress = "127.0.0.1", int port = 8484);
    void stop();

    bool isRunning() const;
    int  port() const;
    QString bindAddress() const;

signals:
    void started(int port);
    void stopped();
    void requestServed(const QString& path, int statusCode);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer* m_server    = nullptr;
    PostStore*  m_posts     = nullptr;
    FeedStore*  m_feed      = nullptr;
    QString     m_bind      = "127.0.0.1";
    int         m_port      = 8484;

    void handleRequest(QTcpSocket* socket, const QString& method,
                       const QString& path, const QMap<QString,QString>& headers);

    // Route handlers — return {statusCode, contentType, body}
    struct Response { int status; QString contentType; QByteArray body; };
    Response serveAggregatedFeed();
    Response serveAuthorFeed(const QString& pubkeyHex);
    Response serveMyFeed();
    Response serveOpmlExport();
    Response serveNotFound();

    // Atom generation
    QByteArray buildAtomFeed(const QString& feedId, const QString& title,
                             const QString& selfUrl, const QJsonArray& posts,
                             const QString& authorName = QString());
    QString    postToAtomEntry(const QJsonObject& post, const QString& authorPubkey,
                               const QString& authorName);
    QString    markdownToHtml(const QString& markdown);  // minimal: escape + line breaks
    QString    isoNow();
    QString    etagFor(const QByteArray& body);          // MD5 hex of body
};
```

---

## blog_ui — IComponent Plugin

### BlogUIComponent

Plugin entry point. Implements `IComponent`. Creates the `QQuickWidget`.

**File:** `src/blog_ui_component.h`

```cpp
#pragma once
#include "i_component.h"
#include <QtPlugin>

class BlogUIComponent : public QObject, public IComponent {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IComponent_iid FILE "ui_metadata.json")
    Q_INTERFACES(IComponent)
public:
    explicit BlogUIComponent(QObject* parent = nullptr);
    QWidget* createWidget(LogosAPI* logosAPI) override;
    void     destroyWidget(QWidget* widget) override;
};
```

---

### BlogBackend

QObject exposed to QML as `backend`. Connects to `blog_module` via QtRO.

**File:** `src/blog_backend.h`

```cpp
#pragma once
#include <QObject>
#include <QStringList>
#include "module_proxy.h"

class BlogBackend : public QObject {
    Q_OBJECT

    // QML-readable properties
    Q_PROPERTY(QString  ownPubkey      READ ownPubkey      NOTIFY identityChanged)
    Q_PROPERTY(QString  displayName    READ displayName    NOTIFY identityChanged)
    Q_PROPERTY(QString  bio            READ bio            NOTIFY identityChanged)
    Q_PROPERTY(bool     rssRunning     READ rssRunning     NOTIFY rssStateChanged)
    Q_PROPERTY(int      rssPort        READ rssPort        NOTIFY rssStateChanged)
    Q_PROPERTY(QString  rssBindAddress READ rssBindAddress NOTIFY rssStateChanged)
    Q_PROPERTY(bool     wakuConnected  READ wakuConnected  NOTIFY wakuStateChanged)

public:
    explicit BlogBackend(QObject* parent = nullptr);

    void initLogos(LogosAPI* api);

    // Property getters
    QString ownPubkey()      const;
    QString displayName()    const;
    QString bio()            const;
    bool    rssRunning()     const;
    int     rssPort()        const;
    QString rssBindAddress() const;
    bool    wakuConnected()  const;

    // Invokable from QML

    // Identity
    Q_INVOKABLE void setIdentity(const QString& displayName, const QString& bio);
    Q_INVOKABLE void generateNewKeypair();  // emits identityChanged when done

    // Posts — return JSON strings (QML parses with JSON.parse())
    Q_INVOKABLE QString createPost(const QString& title, const QString& body,
                                   const QString& summary, const QStringList& tags);
    Q_INVOKABLE bool    updatePost(const QString& id, const QString& title,
                                   const QString& body, const QString& summary,
                                   const QStringList& tags);
    Q_INVOKABLE bool    publishPost(const QString& id);
    Q_INVOKABLE bool    deletePost(const QString& id);
    Q_INVOKABLE QString getPost(const QString& id);
    Q_INVOKABLE QString listPosts();
    Q_INVOKABLE QString listDrafts();

    // Feed
    Q_INVOKABLE bool    subscribe(const QString& pubkey, const QString& displayName);
    Q_INVOKABLE bool    unsubscribe(const QString& pubkey);
    Q_INVOKABLE QString listSubscriptions();
    Q_INVOKABLE QString getAggregatedFeed();
    Q_INVOKABLE QString getFeedByAuthor(const QString& pubkey);

    // Settings
    Q_INVOKABLE bool    setRssPort(int port);
    Q_INVOKABLE bool    setRssBindAddress(const QString& address);

signals:
    void identityChanged();
    void rssStateChanged();
    void wakuStateChanged();
    void postPublished(const QString& postJson);
    void postReceived(const QString& postJson);
    void postDeleted(const QString& postId, const QString& authorPubkey);
    void profileUpdated(const QString& pubkey, const QString& profileJson);
    void errorOccurred(const QString& message);

private:
    ModuleProxy* m_blogModule = nullptr;
    LogosAPI*    m_api        = nullptr;

    QString m_ownPubkey;
    QString m_displayName;
    QString m_bio;
    bool    m_rssRunning     = false;
    int     m_rssPort        = 8484;
    QString m_rssBindAddress = "127.0.0.1";
    bool    m_wakuConnected  = false;

    void connectSignals();
    void refreshIdentity();
    void refreshRssState();
};
```

---

## QtRO Connection Pattern

`blog_module` exposes `BlogPlugin` via QtRO. `BlogBackend` in `blog_ui` connects to it through `ModuleProxy`.

```
blog_ui (logos-app process)
  BlogBackend
    └── ModuleProxy("blog_module")
          └── QtRO socket → logos_host process
                              └── BlogPlugin
```

`BlogBackend::initLogos`:
```cpp
void BlogBackend::initLogos(LogosAPI* api) {
    m_api = api;
    m_blogModule = api->getClient("blog_module");
    if (!m_blogModule) {
        emit errorOccurred("blog_module not available");
        return;
    }
    connectSignals();
    refreshIdentity();
    refreshRssState();
}
```

`connectSignals` registers for async events:
```cpp
void BlogBackend::connectSignals() {
    m_api->on("blog_module", "postPublished", [this](QVariantList args) {
        emit postPublished(args.value(0).toString());
    });
    m_api->on("blog_module", "postReceived", [this](QVariantList args) {
        emit postReceived(args.value(0).toString());
    });
    m_api->on("blog_module", "postDeleted", [this](QVariantList args) {
        emit postDeleted(args.value(0).toString(), args.value(1).toString());
    });
    m_api->on("blog_module", "profileUpdated", [this](QVariantList args) {
        emit profileUpdated(args.value(0).toString(), args.value(1).toString());
    });
    m_api->on("blog_module", "identityChanged", [this](QVariantList) {
        refreshIdentity();
    });
}
```

---

## QML Components

All QML files are embedded in `qml/blog_ui.qrc` with prefix `/blog_ui`.

### Component Inventory

| File | Purpose |
|------|---------|
| `Main.qml` | Root; StackView + sidebar navigation |
| `FeedView.qml` | Aggregated timeline of all subscriptions |
| `BlogView.qml` | Single author's posts |
| `EditorView.qml` | Markdown editor + live preview |
| `PostView.qml` | Single post rendered HTML |
| `DraftsView.qml` | List of own unpublished drafts |
| `MyPostsView.qml` | Own published posts |
| `SettingsView.qml` | Identity, subscriptions, RSS config |
| `SubscribeDialog.qml` | Add subscription by pubkey |
| `components/PostCard.qml` | Post card in feed/list views |
| `components/AuthorChip.qml` | Avatar + display name + pubkey abbreviation |
| `components/TagChip.qml` | Clickable tag pill |
| `components/MarkdownText.qml` | TextEdit in read-only mode rendering markdown |
| `components/SidebarButton.qml` | Navigation button in sidebar |
| `components/ErrorBanner.qml` | Dismissable error strip |

### Main.qml — Structure

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationItem {
    id: root

    // Injected by createWidget()
    property var backend   // BlogBackend*

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left sidebar — 56px wide
        Sidebar {
            id: sidebar
            Layout.preferredWidth: 56
            Layout.fillHeight: true
            currentView: stackView.currentItem?.viewId ?? "feed"
            onNavigate: (viewId) => stackView.navigateTo(viewId)
        }

        // Main content — fills remaining width
        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: feedViewComponent

            function navigateTo(viewId) { /* push appropriate component */ }
        }
    }

    // Connections from backend signals
    Connections {
        target: backend
        function onPostReceived(postJson) { feedView.refresh() }
        function onErrorOccurred(msg)     { errorBanner.show(msg) }
    }
}
```

### PostCard.qml — Key Properties

```qml
// components/PostCard.qml
Item {
    property string postId
    property string title
    property string summary
    property string authorPubkey
    property string authorName
    property string createdAt    // ISO 8601
    property var    tags         // string[]

    signal clicked(string postId, string authorPubkey)
    signal tagClicked(string tag)
    signal authorClicked(string pubkey)
}
```

### EditorView.qml — Key Properties

```qml
// EditorView.qml
Item {
    property string draftId     // "" = new post
    property string title
    property string body        // markdown source
    property string summary
    property var    tags

    signal postPublished(string postId)
    signal saved(string draftId)

    // Internal split: TextArea (left) + WebView/MarkdownText (right)
    // Auto-save to draft every 30 seconds (Timer)
}
```
