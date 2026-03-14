# ── Config ───────────────────────────────────────────────────────────────────
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

BUILD_UI_DIR   ?= build-ui
BUILD_MOD_DIR  ?= build-module
BUILD_TEST_DIR ?= build-tests

# ── Phony targets ─────────────────────────────────────────────────────────────
.PHONY: all build-module build-ui install-module install-ui \
        install install-all setup-nix-merged clean \
        install-kv-module install-delivery-module \
        build-tests test

# ── Nix SDK merge ─────────────────────────────────────────────────────────────
setup-nix-merged:
	@echo "-> Merging Nix SDK paths..."
	rm -rf /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
	mkdir -p /tmp/logos-cpp-sdk-merged/{include,lib} /tmp/logos-liblogos-merged/{include,lib}
	@[ -n "$(LOGOS_SDK_HEADERS_NIX)" ] || (echo "ERROR: logos-cpp-sdk-headers not found in /nix/store"; exit 1)
	ln -sf $(LOGOS_SDK_HEADERS_NIX)/include/* /tmp/logos-cpp-sdk-merged/include/
	ln -sf $(LOGOS_SDK_LIB_NIX)/lib/*         /tmp/logos-cpp-sdk-merged/lib/
	ln -sf $(LOGOS_HEADERS_NIX)/include/*     /tmp/logos-liblogos-merged/include/
	ln -sf $(LOGOS_LIB_NIX)/lib/*             /tmp/logos-liblogos-merged/lib/

# ── Headless module ───────────────────────────────────────────────────────────
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
	@echo "blog_module installed to $(MODULES_DIR)/blog_module"

# ── UI plugin ─────────────────────────────────────────────────────────────────
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
	@echo "blog_ui installed to $(PLUGINS_DIR)/blog_ui"

# ── Dependencies ──────────────────────────────────────────────────────────────
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

# ── Compound targets ──────────────────────────────────────────────────────────
install: install-module install-ui
	@echo "All blog targets installed"

install-all: install install-kv-module install-delivery-module
	@echo "All targets including dependencies installed"
	@echo "Run: cd ~/logos-workspace && nix run '.#logos-app-poc'"

# ── Unit tests (no Logos SDK, no display required) ────────────────────────────
build-tests:
	mkdir -p $(BUILD_TEST_DIR)
	cd $(BUILD_TEST_DIR) && cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
		$(if $(NIX_QTBASE), \
		  -DCMAKE_PREFIX_PATH="$(NIX_QT_PREFIX)" \
		  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$(NIX_QTDECL)$$(echo ';')$(NIX_QTREMOBJ)") \
		&& cmake --build . -j$$(nproc)

test: build-tests
	cd $(BUILD_TEST_DIR) && ctest --output-on-failure

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_UI_DIR) $(BUILD_MOD_DIR) $(BUILD_TEST_DIR) \
	       /tmp/logos-cpp-sdk-merged /tmp/logos-liblogos-merged
