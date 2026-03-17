cmake_minimum_required(VERSION 3.21)
project(logos-blog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(BUILD_MODULE    "Build headless blog_module plugin"  OFF)
option(BUILD_UI_PLUGIN "Build IComponent blog_ui plugin"    OFF)
option(BUILD_TESTS     "Build QTest unit tests (no SDK needed)" OFF)

# Paths injected by Makefile or Nix (required for both targets)
set(LOGOS_CPP_SDK_ROOT  "" CACHE PATH "Path to logos-cpp-sdk merged prefix")
set(LOGOS_LIBLOGOS_ROOT "" CACHE PATH "Path to logos-liblogos merged prefix")

# ── Shared setup ─────────────────────────────────────────────────────────────
if(BUILD_MODULE OR BUILD_UI_PLUGIN)
    find_package(OpenSSL REQUIRED)
    set(CMAKE_AUTOMOC ON)

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

# ── Target: blog_module (headless) ───────────────────────────────────────────
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
        src/chat_sync.cpp
        src/storage_sync.cpp
        src/rss_server.cpp
        src/crypto.cpp
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
        OpenSSL::Crypto
        ${LOGOS_SDK_LIB}
        Qt6::RemoteObjects   # listed twice: SDK pulls RemoteObjects symbols
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

# ── Target: blog_ui (IComponent) ─────────────────────────────────────────────
if(BUILD_UI_PLUGIN)
    set(CMAKE_AUTORCC ON)   # REQUIRED for .qrc embedding

    find_package(Qt6 REQUIRED COMPONENTS
        Core Qml Quick QuickWidgets Widgets RemoteObjects Network
    )

    add_library(blog_ui SHARED
        src/blog_ui_component.cpp
        src/blog_backend.cpp
        src/crypto.cpp
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
        OpenSSL::Crypto
        ${LOGOS_SDK_LIB}
        Qt6::RemoteObjects
        ${LOGOS_CORE_LIB}
    )

    set_target_properties(blog_ui PROPERTIES
        PREFIX ""
        OUTPUT_NAME "blog_ui"
    )
endif()

# ── Target: unit tests (BUILD_TESTS=ON, no Logos SDK required) ───────────────
if(BUILD_TESTS)
    find_package(OpenSSL REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
    set(CMAKE_AUTOMOC ON)
    enable_testing()

    # Common include dirs for all test targets:
    #   tests/ first  → provides module_proxy.h stub (shadows the real SDK header)
    #   src/          → provides all application headers
    set(TEST_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    # ── helper macro ─────────────────────────────────────────────────────────
    macro(add_blog_test target sources)
        add_executable(${target} ${sources})
        target_include_directories(${target} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_definitions(${target} PRIVATE BUILD_TESTS)
        target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test OpenSSL::Crypto)
        add_test(NAME ${target} COMMAND ${target})
    endmacro()

    # ── test_crypto ───────────────────────────────────────────────────────────
    add_blog_test(test_crypto
        "tests/test_crypto.cpp;src/crypto.cpp"
    )

    # ── test_post_store ───────────────────────────────────────────────────────
    add_blog_test(test_post_store
        "tests/test_post_store.cpp;src/post_store.cpp"
    )

    # ── test_feed_store ───────────────────────────────────────────────────────
    add_blog_test(test_feed_store
        "tests/test_feed_store.cpp;src/feed_store.cpp;src/crypto.cpp"
    )

    # ── test_rss_server ───────────────────────────────────────────────────────
    # Needs Qt6::Network for QTcpServer used by RssServer
    add_executable(test_rss_server
        tests/test_rss_server.cpp
        src/rss_server.cpp
        src/post_store.cpp
        src/feed_store.cpp
        src/crypto.cpp
    )
    target_include_directories(test_rss_server PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_definitions(test_rss_server PRIVATE BUILD_TESTS)
    target_link_libraries(test_rss_server PRIVATE
        Qt6::Core Qt6::Network Qt6::Test OpenSSL::Crypto)
    add_test(NAME test_rss_server COMMAND test_rss_server)

    # ── test_blog_plugin ──────────────────────────────────────────────────────
    # Tests standalone envelope/search logic — only needs crypto.cpp
    add_blog_test(test_blog_plugin
        "tests/test_blog_plugin.cpp;src/crypto.cpp"
    )
endif()

# ── Target: visual/screenshot tests (BUILD_VISUAL_TESTS=ON) ──────────────────
# No Logos SDK required. Needs Qt6::Quick + a real or virtual display at run time.
option(BUILD_VISUAL_TESTS "Build Qt Quick headless screenshot tests" OFF)

if(BUILD_VISUAL_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Test)
    set(CMAKE_AUTOMOC ON)

    add_executable(test_visual tests/test_visual.cpp)

    target_include_directories(test_visual PRIVATE tests)

    target_compile_definitions(test_visual PRIVATE
        BLOG_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
        DEFAULT_SCREENSHOTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/screenshots"
    )

    target_link_libraries(test_visual PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::Test
    )

    # Register with ctest so `ctest` can run it (needs DISPLAY to be set)
    add_test(NAME test_visual COMMAND test_visual)
endif()
