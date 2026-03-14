cmake_minimum_required(VERSION 3.21)
project(logos-blog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(BUILD_MODULE    "Build headless blog_module plugin"  OFF)
option(BUILD_UI_PLUGIN "Build IComponent blog_ui plugin"    OFF)

# Paths injected by Makefile or Nix (required for both targets)
set(LOGOS_CPP_SDK_ROOT  "" CACHE PATH "Path to logos-cpp-sdk merged prefix")
set(LOGOS_LIBLOGOS_ROOT "" CACHE PATH "Path to logos-liblogos merged prefix")

# ── Shared setup ─────────────────────────────────────────────────────────────
if(BUILD_MODULE OR BUILD_UI_PLUGIN)
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
