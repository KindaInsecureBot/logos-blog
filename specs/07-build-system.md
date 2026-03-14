# Logos Blog — Build System

## Directory Structure

```
logos-blog/
├── CMakeLists.txt              root — orchestrates both targets
├── flake.nix
├── module.yaml
├── Makefile
├── metadata.json               headless module manifest
├── ui_metadata.json            IComponent UI manifest
├── src/
│   ├── blog_plugin.h/.cpp      BlogPlugin (headless)
│   ├── post_store.h/.cpp
│   ├── feed_store.h/.cpp
│   ├── waku_sync.h/.cpp
│   ├── rss_server.h/.cpp
│   ├── blog_ui_component.h/.cpp  BlogUIComponent (IComponent)
│   └── blog_backend.h/.cpp
├── qml/
│   ├── blog_ui.qrc
│   ├── Main.qml
│   ├── FeedView.qml
│   ├── BlogView.qml
│   ├── PostView.qml
│   ├── EditorView.qml
│   ├── MyPostsView.qml
│   ├── DraftsView.qml
│   ├── SettingsView.qml
│   ├── SubscribeDialog.qml
│   └── components/
│       ├── PostCard.qml
│       ├── AuthorChip.qml
│       ├── TagChip.qml
│       ├── MarkdownText.qml
│       ├── SidebarButton.qml
│       └── ErrorBanner.qml
└── specs/
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(logos-blog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ─────────────────────────────────────────────────────────────────
option(BUILD_MODULE    "Build headless blog_module plugin"  OFF)
option(BUILD_UI_PLUGIN "Build IComponent blog_ui plugin"    OFF)

# Paths injected by Makefile or Nix (required for both targets)
set(LOGOS_CPP_SDK_ROOT  "" CACHE PATH "Path to logos-cpp-sdk merged prefix")
set(LOGOS_LIBLOGOS_ROOT "" CACHE PATH "Path to logos-liblogos merged prefix")

# ── Shared setup ─────────────────────────────────────────────────────────────
if(BUILD_MODULE OR BUILD_UI_PLUGIN)
    set(CMAKE_AUTOMOC ON)

    # Common include dirs
    set(LOGOS_INCLUDE_DIRS
        "${LOGOS_CPP_SDK_ROOT}/include"
        "${LOGOS_CPP_SDK_ROOT}/include/cpp"
        "${LOGOS_LIBLOGOS_ROOT}/include"
    )

    # logos_sdk static lib (from logos-cpp-sdk)
    find_library(LOGOS_SDK_LIB logos_sdk
        HINTS "${LOGOS_CPP_SDK_ROOT}/lib"
        REQUIRED
    )
    # logos_core shared lib (from logos-liblogos)
    find_library(LOGOS_CORE_LIB logos_core
        HINTS "${LOGOS_LIBLOGOS_ROOT}/lib"
        REQUIRED
    )
endif()

# ── Target: blog_module (headless) ──────────────────────────────────────────
if(BUILD_MODULE)
    find_package(Qt6 REQUIRED COMPONENTS Core Qml RemoteObjects Network)
    # NO Quick, NO Widgets — headless must not link display modules

    qt_add_plugin(blog_module_plugin
        CLASS_NAME BlogPlugin
        PLUGIN_TYPE generic
    )

    target_sources(blog_module_plugin PRIVATE
        src/blog_plugin.cpp
        src/post_store.cpp
        src/feed_store.cpp
        src/waku_sync.cpp
        src/rss_server.cpp
    )

    target_include_directories(blog_module_plugin PRIVATE
        src
        ${LOGOS_INCLUDE_DIRS}
    )

    # logos_sdk BEFORE Qt6::RemoteObjects (linker ordering)
    target_link_libraries(blog_module_plugin PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::RemoteObjects
        Qt6::Network
        ${LOGOS_SDK_LIB}
        Qt6::RemoteObjects   # listed twice intentionally: SDK pulls RemoteObjects symbols
        ${LOGOS_CORE_LIB}
    )

    target_compile_definitions(blog_module_plugin PRIVATE
        BLOG_MODULE_BUILD
    )

    set_target_properties(blog_module_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "blog_module_plugin"
    )
endif()

# ── Target: blog_ui (IComponent) ────────────────────────────────────────────
if(BUILD_UI_PLUGIN)
    set(CMAKE_AUTORCC ON)   # REQUIRED for .qrc embedding

    find_package(Qt6 REQUIRED COMPONENTS
        Core Qml Quick QuickWidgets Widgets RemoteObjects Network
    )

    add_library(blog_ui SHARED
        src/blog_ui_component.cpp
        src/blog_backend.cpp
        qml/blog_ui.qrc
    )

    target_include_directories(blog_ui PRIVATE
        src
        ${LOGOS_INCLUDE_DIRS}
    )

    target_compile_definitions(blog_ui PRIVATE
        BLOG_UI_BUILD                # guards Q_PLUGIN_METADATA in non-entry-point classes
        LOGOS_CORE_AVAILABLE         # enables initLogos() call in createWidget()
        BLOG_UI_METADATA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/ui_metadata.json"
    )

    # logos_sdk BEFORE Qt6::RemoteObjects
    target_link_libraries(blog_ui PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickWidgets
        Qt6::Widgets
        Qt6::RemoteObjects
        Qt6::Network
        ${LOGOS_SDK_LIB}
        Qt6::RemoteObjects
        ${LOGOS_CORE_LIB}
    )

    set_target_properties(blog_ui PROPERTIES
        PREFIX ""
        OUTPUT_NAME "blog_ui"
    )
endif()
```

---

## QML Resource File

**`qml/blog_ui.qrc`**:
```xml
<RCC>
    <qresource prefix="/blog_ui">
        <file>Main.qml</file>
        <file>FeedView.qml</file>
        <file>BlogView.qml</file>
        <file>PostView.qml</file>
        <file>EditorView.qml</file>
        <file>MyPostsView.qml</file>
        <file>DraftsView.qml</file>
        <file>SettingsView.qml</file>
        <file>SubscribeDialog.qml</file>
        <file>components/PostCard.qml</file>
        <file>components/AuthorChip.qml</file>
        <file>components/TagChip.qml</file>
        <file>components/MarkdownText.qml</file>
        <file>components/SidebarButton.qml</file>
        <file>components/ErrorBanner.qml</file>
    </qresource>
</RCC>
```

QML source is loaded in `BlogUIComponent::createWidget`:
```cpp
quickWidget->setSource(QUrl("qrc:/blog_ui/Main.qml"));
```

---

## Plugin Manifests

### `metadata.json` — Headless Module

```json
{
  "name": "blog_module",
  "version": "0.1.0",
  "type": "core",
  "category": "productivity",
  "author": "logos-co",
  "description": "Decentralized blogging backend (headless)",
  "dependencies": ["kv_module", "delivery_module"],
  "main": "blog_module_plugin",
  "manifestVersion": "0.1.0"
}
```

### `ui_metadata.json` — IComponent

```json
{
  "name": "blog_ui",
  "version": "0.1.0",
  "type": "ui",
  "category": "productivity",
  "author": "logos-co",
  "description": "Decentralized blogging UI for Logos",
  "dependencies": ["blog_module"],
  "main": {
    "linux-amd64":   "blog_ui.so",
    "linux-aarch64": "blog_ui.so",
    "darwin-arm64":  "blog_ui.so"
  },
  "manifestVersion": "0.1.0"
}
```

---

## Nix Flake

**`flake.nix`**:
```nix
{
  description = "Logos Blog — decentralized blogging IComponent plugin";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows           = "logos-module-builder/nixpkgs";
    logos-cpp-sdk = {
      url    = "github:logos-co/logos-cpp-sdk";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    logos-liblogos = {
      url    = "github:logos-co/logos-liblogos";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    };
  };

  outputs = { self, logos-module-builder, nixpkgs, logos-cpp-sdk, logos-liblogos, ... }:
    let
      moduleOutputs = logos-module-builder.lib.mkLogosModule {
        src        = ./.;
        configFile = ./module.yaml;
      };

      forAllSystems = f: nixpkgs.lib.genAttrs
        [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ]
        (system: f {
          pkgs          = import nixpkgs { inherit system; };
          logosSdk      = logos-cpp-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
        });

    in moduleOutputs // {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos }:
        let
          base = moduleOutputs.packages.${pkgs.system} or {};

          commonCmakeFlags = [
            "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
            "-DLOGOS_LIBLOGOS_ROOT=${logosLiblogos}"
          ];

          blog-module = pkgs.stdenv.mkDerivation {
            pname   = "blog-module";
            version = "0.1.0";
            src     = ./.;
            nativeBuildInputs = with pkgs; [ cmake ninja ];
            buildInputs = with pkgs.qt6; [ qtbase qtdeclarative qtremoteobjects ];
            cmakeFlags  = commonCmakeFlags ++ [ "-DBUILD_MODULE=ON" ];
            buildPhase  = "cmake --build . --target blog_module_plugin -j$NIX_BUILD_CORES";
            installPhase = ''
              mkdir -p $out/lib
              cp blog_module_plugin.so $out/lib/
              cp ${self}/metadata.json $out/lib/manifest.json
            '';
            dontWrapQtApps = true;
          };

          blog-ui = pkgs.stdenv.mkDerivation {
            pname   = "blog-ui";
            version = "0.1.0";
            src     = ./.;
            nativeBuildInputs = with pkgs; [ cmake ninja pkgs.qt6.wrapQtAppsHook ];
            buildInputs = with pkgs.qt6; [
              qtbase qtdeclarative qtremoteobjects qtquickcontrols2
            ];
            cmakeFlags  = commonCmakeFlags ++ [ "-DBUILD_UI_PLUGIN=ON" ];
            buildPhase  = "cmake --build . --target blog_ui -j$NIX_BUILD_CORES";
            installPhase = ''
              mkdir -p $out/lib
              cp blog_ui.so $out/lib/
              cp ${self}/ui_metadata.json $out/lib/manifest.json
            '';
            dontWrapQtApps = true;
          };

        in base // { inherit blog-module blog-ui; }
      );
    };
}
```

Build locally:
```bash
nix build 'path:.#blog-ui'     # builds blog_ui.so
nix build 'path:.#blog-module' # builds blog_module_plugin.so
```

---

## module.yaml

Used by `logos-module-builder`:

```yaml
name: blog_module
version: 0.1.0
type: core
description: Decentralized blogging backend for Logos
dependencies:
  - kv_module
  - delivery_module
build:
  cmake:
    flags:
      - -DBUILD_MODULE=ON
    target: blog_module_plugin
output:
  lib: blog_module_plugin.so
  manifest: metadata.json
```

---

## Makefile

```makefile
# ── Config ──────────────────────────────────────────────────────────────────
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug

# Auto-detect Nix store Qt 6 paths
NIX_QTBASE   ?= $(shell ls -d /nix/store/*-qtbase-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QTDECL   ?= $(shell ls -d /nix/store/*-qtdeclarative-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QTREMOBJ ?= $(shell ls -d /nix/store/*-qtremoteobjects-6.* 2>/dev/null | grep -v '\.drv$$' | grep -v dev | head -1)
NIX_QT_PREFIX ?= $(NIX_QTBASE);$(NIX_QTDECL);$(NIX_QTREMOBJ)

# Auto-detect Nix store Logos SDK paths
LOGOS_HEADERS_NIX     ?= $(shell ls -d /nix/store/*logos-liblogos-headers-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_LIB_NIX         ?= $(shell ls -d /nix/store/*logos-liblogos-lib-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_SDK_HEADERS_NIX ?= $(shell ls -d /nix/store/*logos-cpp-sdk-headers-* 2>/dev/null | grep -v '\.drv$$' | head -1)
LOGOS_SDK_LIB_NIX     ?= $(shell ls -d /nix/store/*logos-cpp-sdk-lib-* 2>/dev/null | grep -v '\.drv$$' | head -1)

MODULES_DIR ?= $(HOME)/.local/share/Logos/LogosAppNix/modules
PLUGINS_DIR ?= $(HOME)/.local/share/Logos/LogosAppNix/plugins

BUILD_UI_DIR  ?= build-ui
BUILD_MOD_DIR ?= build-module

# ── Phony targets ────────────────────────────────────────────────────────────
.PHONY: all build-module build-ui install-module install-ui \
        install install-all setup-nix-merged clean \
        install-kv-module install-delivery-module

# ── Nix SDK merge ────────────────────────────────────────────────────────────
setup-nix-merged:
	@echo "→ Merging Nix SDK paths..."
	rm -rf /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
	mkdir -p /tmp/logos-cpp-sdk-merged/{include,lib} /tmp/logos-liblogos-merged/{include,lib}
	@[ -n "$(LOGOS_SDK_HEADERS_NIX)" ] || (echo "ERROR: logos-cpp-sdk-headers not found in /nix/store"; exit 1)
	ln -sf $(LOGOS_SDK_HEADERS_NIX)/include/* /tmp/logos-cpp-sdk-merged/include/
	ln -sf $(LOGOS_SDK_LIB_NIX)/lib/*         /tmp/logos-cpp-sdk-merged/lib/
	ln -sf $(LOGOS_HEADERS_NIX)/include/*     /tmp/logos-liblogos-merged/include/
	ln -sf $(LOGOS_LIB_NIX)/lib/*             /tmp/logos-liblogos-merged/lib/

# ── Headless module ──────────────────────────────────────────────────────────
build-module: setup-nix-merged
	mkdir -p $(BUILD_MOD_DIR)
	cd $(BUILD_MOD_DIR) && cmake .. $(CMAKE_FLAGS) \
		-DBUILD_MODULE=ON \
		-DLOGOS_CPP_SDK_ROOT=/tmp/logos-cpp-sdk-merged \
		-DLOGOS_LIBLOGOS_ROOT=/tmp/logos-liblogos-merged \
		$(if $(NIX_QTBASE), \
		  -DCMAKE_PREFIX_PATH="$(NIX_QT_PREFIX)" \
		  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$(NIX_QTDECL)$$(echo ';')$(NIX_QTREMOBJ)") \
		&& cmake --build . --target blog_module_plugin -j$$(nproc)

install-module: build-module
	mkdir -p $(MODULES_DIR)/blog_module
	cp $(BUILD_MOD_DIR)/blog_module_plugin.so $(MODULES_DIR)/blog_module/
	cp metadata.json $(MODULES_DIR)/blog_module/manifest.json
	@echo "✓ blog_module installed"

# ── UI plugin ────────────────────────────────────────────────────────────────
build-ui: setup-nix-merged
	mkdir -p $(BUILD_UI_DIR)
	cd $(BUILD_UI_DIR) && cmake .. $(CMAKE_FLAGS) \
		-DBUILD_UI_PLUGIN=ON \
		-DLOGOS_CPP_SDK_ROOT=/tmp/logos-cpp-sdk-merged \
		-DLOGOS_LIBLOGOS_ROOT=/tmp/logos-liblogos-merged \
		$(if $(NIX_QTBASE), \
		  -DCMAKE_PREFIX_PATH="$(NIX_QT_PREFIX)" \
		  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$(NIX_QTDECL)$$(echo ';')$(NIX_QTREMOBJ)") \
		&& cmake --build . --target blog_ui -j$$(nproc)

install-ui: build-ui
	mkdir -p $(PLUGINS_DIR)/blog_ui
	cp $(BUILD_UI_DIR)/blog_ui.so $(PLUGINS_DIR)/blog_ui/
	cp ui_metadata.json $(PLUGINS_DIR)/blog_ui/manifest.json
	@echo "✓ blog_ui installed"

# ── Dependencies ─────────────────────────────────────────────────────────────
LGPM ?= $(HOME)/.local/share/Logos/LogosAppNix/lgpm/bin/lgpm
LGPM_RELEASE ?= build-20260307-a751c91-69

install-kv-module:
	$(LGPM) --release $(LGPM_RELEASE) \
	        --modules-dir $(MODULES_DIR) \
	        install logos-kv-module

install-delivery-module:
	$(LGPM) --release $(LGPM_RELEASE) \
	        --modules-dir $(MODULES_DIR) \
	        install logos-delivery-module

# ── Compound targets ─────────────────────────────────────────────────────────
install: install-module install-ui
	@echo "✓ All blog targets installed"

install-all: install install-kv-module install-delivery-module
	@echo "✓ All targets including dependencies installed"
	@echo "  Run: cd ~/logos-workspace && nix run '.#logos-app-poc'"

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_UI_DIR) $(BUILD_MOD_DIR) \
	       /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
```

---

## Dependencies Summary

| Library | Used By | Qt module |
|---------|---------|-----------|
| Qt6::Core | both | Required everywhere |
| Qt6::Qml | both | QML engine (headless needs it for plugin registration) |
| Qt6::RemoteObjects | both | QtRO source/replica for inter-process communication |
| Qt6::Network | blog_module | `QTcpServer`/`QTcpSocket` for RssServer |
| Qt6::Quick | blog_ui only | QML scene graph |
| Qt6::QuickWidgets | blog_ui only | `QQuickWidget` for embedding in QWidget host |
| Qt6::Widgets | blog_ui only | `QWidget` base class (IComponent requirement) |
| logos_sdk.a | both | LogosAPI, ModuleProxy, PluginInterface, IComponent |
| logos_core.so | both | Runtime transport for QtRO |

**Note:** `Qt6::Network` is **not** included in the headless plugin's `find_package` in many Logos modules — add it explicitly since `QTcpServer` lives there.
