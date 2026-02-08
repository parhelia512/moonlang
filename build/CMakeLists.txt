# MoonScript CMake Build Configuration
# Cross-platform build: Windows and Linux
# Copyright (c) 2026 greenteng.com
#
# Modular runtime:
#   moonrt_core.cpp     - core: types, memory, refcount
#   moonrt_math.cpp     - math
#   moonrt_string.cpp   - string
#   moonrt_list.cpp     - list
#   moonrt_dict.cpp     - dict
#   moonrt_builtin.cpp  - builtins
#   moonrt_io.cpp       - file/path/datetime
#   moonrt_json.cpp     - JSON (optional)
#   moonrt_network.cpp  - network (optional)
#   moonrt_dll.cpp      - DLL load (optional)
#   moonrt_regex.cpp    - PCRE2 regex (optional)
#   moonrt_tls.cpp      - TLS/SSL (OpenSSL, optional)
#   moonrt_async.cpp    - async
#   moonrt_channel.cpp  - Go-style channel
#   moonrt_gui.cpp      - GUI

cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Read version from version.json
# ============================================================================
set(VERSION_JSON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../version.json")

if(EXISTS "${VERSION_JSON_PATH}")
    file(READ "${VERSION_JSON_PATH}" VERSION_JSON_CONTENT)
    
    # Parse version
    string(REGEX MATCH "\"major\"[ ]*:[ ]*([0-9]+)" _ ${VERSION_JSON_CONTENT})
    set(VERSION_MAJOR ${CMAKE_MATCH_1})
    
    string(REGEX MATCH "\"minor\"[ ]*:[ ]*([0-9]+)" _ ${VERSION_JSON_CONTENT})
    set(VERSION_MINOR ${CMAKE_MATCH_1})
    
    string(REGEX MATCH "\"patch\"[ ]*:[ ]*([0-9]+)" _ ${VERSION_JSON_CONTENT})
    set(VERSION_PATCH ${CMAKE_MATCH_1})
    
    set(PROJECT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
    message(STATUS "Version from version.json: ${PROJECT_VERSION_STRING}")
else()
    message(WARNING "version.json not found, using default version 1.0.0")
    set(VERSION_MAJOR 1)
    set(VERSION_MINOR 0)
    set(VERSION_PATCH 0)
    set(PROJECT_VERSION_STRING "1.0.0")
endif()

project(moonscript VERSION ${PROJECT_VERSION_STRING} LANGUAGES CXX C)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build options
option(ENABLE_GUI "Enable GUI/WebView support (requires GTK/WebKit)" ON)
option(ENABLE_PCRE2 "Enable PCRE2 for regex (faster, more features)" ON)
option(ENABLE_TLS "Enable TLS/SSL support (requires OpenSSL)" ON)
option(ENABLE_NETWORK "Enable TCP/UDP networking" ON)
option(ENABLE_DLL "Enable DLL/shared library loading" ON)
option(ENABLE_JSON "Enable JSON support" ON)

# Compile options
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

# Source dirs
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FRONTEND_SRC_DIR "${SRC_DIR}/src/frontend")
set(LLVM_SRC_DIR "${SRC_DIR}/src/llvm")
set(LIB_DIR "${SRC_DIR}/lib")

# ============================================================================
# PCRE2
# ============================================================================

if(ENABLE_PCRE2)
    set(PCRE2_DIR "${LIB_DIR}/pcre2")
    
    if(EXISTS "${PCRE2_DIR}/src/pcre2.h")
        message(STATUS "PCRE2 found at ${PCRE2_DIR}")
        
        # PCRE2 sources (10.45+ has extra files)
        set(PCRE2_SOURCES
            ${PCRE2_DIR}/src/pcre2_auto_possess.c
            ${PCRE2_DIR}/src/pcre2_chartables.c
            ${PCRE2_DIR}/src/pcre2_chkdint.c
            ${PCRE2_DIR}/src/pcre2_compile.c
            ${PCRE2_DIR}/src/pcre2_compile_class.c
            ${PCRE2_DIR}/src/pcre2_compile_cgroup.c
            ${PCRE2_DIR}/src/pcre2_config.c
            ${PCRE2_DIR}/src/pcre2_context.c
            ${PCRE2_DIR}/src/pcre2_convert.c
            ${PCRE2_DIR}/src/pcre2_dfa_match.c
            ${PCRE2_DIR}/src/pcre2_error.c
            ${PCRE2_DIR}/src/pcre2_extuni.c
            ${PCRE2_DIR}/src/pcre2_find_bracket.c
            ${PCRE2_DIR}/src/pcre2_jit_compile.c
            ${PCRE2_DIR}/src/pcre2_maketables.c
            ${PCRE2_DIR}/src/pcre2_match.c
            ${PCRE2_DIR}/src/pcre2_match_data.c
            ${PCRE2_DIR}/src/pcre2_match_next.c
            ${PCRE2_DIR}/src/pcre2_newline.c
            ${PCRE2_DIR}/src/pcre2_ord2utf.c
            ${PCRE2_DIR}/src/pcre2_pattern_info.c
            ${PCRE2_DIR}/src/pcre2_script_run.c
            ${PCRE2_DIR}/src/pcre2_serialize.c
            ${PCRE2_DIR}/src/pcre2_string_utils.c
            ${PCRE2_DIR}/src/pcre2_study.c
            ${PCRE2_DIR}/src/pcre2_substitute.c
            ${PCRE2_DIR}/src/pcre2_substring.c
            ${PCRE2_DIR}/src/pcre2_tables.c
            ${PCRE2_DIR}/src/pcre2_ucd.c
            ${PCRE2_DIR}/src/pcre2_valid_utf.c
            ${PCRE2_DIR}/src/pcre2_xclass.c
        )
        
        # PCRE2 static lib
        add_library(pcre2 STATIC ${PCRE2_SOURCES})
        target_compile_definitions(pcre2 PRIVATE
            PCRE2_CODE_UNIT_WIDTH=8
            PCRE2_STATIC
            HAVE_CONFIG_H
            SUPPORT_UNICODE
        )
        target_include_directories(pcre2 PUBLIC ${PCRE2_DIR}/src)
        
        set(PCRE2_AVAILABLE ON)
        set(PCRE2_INCLUDE_DIR ${PCRE2_DIR}/src)
    else()
        message(WARNING "PCRE2 source not found. Run lib/pcre2/download_pcre2.bat first.")
        message(STATUS "Falling back to std::regex")
        add_definitions(-DMOON_REGEX_STD)
        set(PCRE2_AVAILABLE OFF)
    endif()
else()
    message(STATUS "PCRE2 disabled - using std::regex")
    add_definitions(-DMOON_REGEX_STD)
    set(PCRE2_AVAILABLE OFF)
endif()

# ============================================================================
# OpenSSL/TLS
# ============================================================================

if(ENABLE_TLS)
    set(OPENSSL_DIR "${LIB_DIR}/openssl")
    
    # Prefer local OpenSSL
    if(EXISTS "${OPENSSL_DIR}/lib/libssl.lib" OR EXISTS "${OPENSSL_DIR}/lib/libssl.a")
        message(STATUS "Local OpenSSL found at ${OPENSSL_DIR}")
        
        set(OPENSSL_INCLUDE_DIR "${OPENSSL_DIR}/include")
        set(OPENSSL_AVAILABLE ON)
        
        if(WIN32)
            set(OPENSSL_LIBRARIES
                "${OPENSSL_DIR}/lib/libssl.lib"
                "${OPENSSL_DIR}/lib/libcrypto.lib"
            )
        else()
            set(OPENSSL_LIBRARIES
                "${OPENSSL_DIR}/lib/libssl.a"
                "${OPENSSL_DIR}/lib/libcrypto.a"
            )
        endif()
    else()
        # Try system OpenSSL
        find_package(OpenSSL)
        if(OPENSSL_FOUND)
            message(STATUS "System OpenSSL found: ${OPENSSL_VERSION}")
            set(OPENSSL_AVAILABLE ON)
            set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
        else()
            message(WARNING "OpenSSL not found - TLS support disabled")
            message(STATUS "To enable TLS, install OpenSSL libraries to lib/openssl/")
            add_definitions(-DMOON_NO_TLS)
            set(OPENSSL_AVAILABLE OFF)
        endif()
    endif()
else()
    message(STATUS "TLS support disabled")
    add_definitions(-DMOON_NO_TLS)
    set(OPENSSL_AVAILABLE OFF)
endif()

# ============================================================================
# Feature flags
# ============================================================================

if(NOT ENABLE_NETWORK)
    add_definitions(-DMOON_NO_NETWORK)
endif()

if(NOT ENABLE_DLL)
    add_definitions(-DMOON_NO_DLL)
endif()

if(NOT ENABLE_JSON)
    add_definitions(-DMOON_NO_JSON)
endif()

# ============================================================================
# Runtime sources (moonrt.cpp is the single entry that #includes all modules)
# ============================================================================

# moonrt.cpp #includes:
#   moonrt_core.cpp, moonrt_math.cpp, moonrt_string.cpp,
#   moonrt_list.cpp, moonrt_dict.cpp, moonrt_builtin.cpp,
#   moonrt_io.cpp, moonrt_json.cpp, moonrt_network.cpp, moonrt_dll.cpp

set(MOONRT_SOURCES
    ${LLVM_SRC_DIR}/moonrt.cpp
    ${LLVM_SRC_DIR}/moonrt_async.cpp
    ${LLVM_SRC_DIR}/moonrt_channel.cpp
    ${LLVM_SRC_DIR}/moonrt_regex.cpp
    ${LLVM_SRC_DIR}/moonrt_tls.cpp
)

# ============================================================================
# Platform
# ============================================================================

if(WIN32)
    message(STATUS "Building for Windows")
    
    # Windows GUI sources
    set(MOONRT_GUI_SOURCES ${LLVM_SRC_DIR}/moonrt_gui.cpp)
    
    # Windows libs
    set(PLATFORM_LIBS
        ws2_32
        user32
        shell32
        ole32
        dwmapi
        shlwapi
        gdi32
        advapi32
    )
    
    # Unicode
    add_definitions(-DUNICODE -D_UNICODE)
    
    # MSVC
    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
    endif()
    
    # WebView2 include (if present)
    if(EXISTS "${SRC_DIR}/webview2/include")
        include_directories("${SRC_DIR}/webview2/include")
    endif()

elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
    
    # Linux base libs
    set(PLATFORM_LIBS pthread dl)
    
    if(ENABLE_GUI)
        message(STATUS "GUI support enabled - looking for GTK/WebKit")
        
        # Linux GUI sources
        set(MOONRT_GUI_SOURCES ${LLVM_SRC_DIR}/moonrt_gui_linux.cpp)
        
        # Required packages
        find_package(PkgConfig REQUIRED)
        
        # GTK3
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        include_directories(${GTK3_INCLUDE_DIRS})
        link_directories(${GTK3_LIBRARY_DIRS})
        add_definitions(${GTK3_CFLAGS_OTHER})
        
        # WebKitGTK (4.0 and 4.1)
        pkg_check_modules(WEBKIT webkit2gtk-4.1)
        if(NOT WEBKIT_FOUND)
            pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.0)
        endif()
        message(STATUS "Using WebKitGTK: ${WEBKIT_VERSION}")
        include_directories(${WEBKIT_INCLUDE_DIRS})
        link_directories(${WEBKIT_LIBRARY_DIRS})
        add_definitions(${WEBKIT_CFLAGS_OTHER})
        
        # AppIndicator (optional)
        pkg_check_modules(APPINDICATOR appindicator3-0.1)
        if(APPINDICATOR_FOUND)
            include_directories(${APPINDICATOR_INCLUDE_DIRS})
            link_directories(${APPINDICATOR_LIBRARY_DIRS})
            add_definitions(-DHAVE_APPINDICATOR ${APPINDICATOR_CFLAGS_OTHER})
            message(STATUS "AppIndicator found - system tray support enabled")
        else()
            message(STATUS "AppIndicator not found - system tray support disabled")
        endif()
        
        # GUI libs
        list(APPEND PLATFORM_LIBS ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES})
        
        if(APPINDICATOR_FOUND)
            list(APPEND PLATFORM_LIBS ${APPINDICATOR_LIBRARIES})
        endif()
    else()
        message(STATUS "GUI support disabled - no GTK/WebKit dependencies")
        add_definitions(-DMOON_NO_GUI)
        set(MOONRT_GUI_SOURCES ${LLVM_SRC_DIR}/moonrt_gui_linux.cpp)
    endif()

elseif(APPLE)
    message(STATUS "Building for macOS")
    
    # macOS specific definitions
    add_definitions(-DMOON_PLATFORM_MACOS)
    
    if(ENABLE_GUI)
        message(STATUS "GUI support enabled - using AppKit + WKWebView")
        
        # macOS GUI uses Objective-C++ (.mm file)
        set(MOONRT_GUI_SOURCES ${LLVM_SRC_DIR}/moonrt_gui_macos.mm)
        
        # Enable Objective-C++ for .mm files
        enable_language(OBJCXX)
        set(CMAKE_OBJCXX_STANDARD 17)
        
        # Find required frameworks
        find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
        find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
        find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
        
        set(PLATFORM_LIBS
            pthread
            ${COCOA_FRAMEWORK}
            ${WEBKIT_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
        )
    else()
        message(STATUS "GUI support disabled - no AppKit/WebKit dependencies")
        add_definitions(-DMOON_NO_GUI)
        set(MOONRT_GUI_SOURCES ${LLVM_SRC_DIR}/moonrt_gui_macos.mm)
        
        set(PLATFORM_LIBS pthread)
    endif()
    
    # OpenSSL for macOS (via Homebrew or system)
    if(ENABLE_TLS)
        # Try Homebrew locations first
        set(HOMEBREW_OPENSSL_ROOT "/usr/local/opt/openssl@3")
        set(HOMEBREW_OPENSSL_ROOT_ARM "/opt/homebrew/opt/openssl@3")
        
        if(EXISTS "${HOMEBREW_OPENSSL_ROOT}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR ${HOMEBREW_OPENSSL_ROOT})
            message(STATUS "Found Homebrew OpenSSL at ${HOMEBREW_OPENSSL_ROOT}")
        elseif(EXISTS "${HOMEBREW_OPENSSL_ROOT_ARM}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR ${HOMEBREW_OPENSSL_ROOT_ARM})
            message(STATUS "Found Homebrew OpenSSL at ${HOMEBREW_OPENSSL_ROOT_ARM}")
        endif()
        
        find_package(OpenSSL)
        if(OPENSSL_FOUND)
            message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
            set(OPENSSL_AVAILABLE ON)
            list(APPEND PLATFORM_LIBS OpenSSL::SSL OpenSSL::Crypto)
        else()
            message(WARNING "OpenSSL not found - TLS support disabled")
            message(STATUS "To enable TLS, install OpenSSL: brew install openssl@3")
            add_definitions(-DMOON_NO_TLS)
            set(OPENSSL_AVAILABLE OFF)
        endif()
    endif()
endif()

# ============================================================================
# Runtime library
# ============================================================================

add_library(moonrt STATIC
    ${MOONRT_SOURCES}
    ${MOONRT_GUI_SOURCES}
)

target_include_directories(moonrt PRIVATE
    ${LLVM_SRC_DIR}
    ${SRC_DIR}
    ${FRONTEND_SRC_DIR}
)

# PCRE2
if(PCRE2_AVAILABLE)
    target_include_directories(moonrt PRIVATE ${PCRE2_INCLUDE_DIR})
    target_link_libraries(moonrt pcre2)
else()
    target_compile_definitions(moonrt PRIVATE MOON_REGEX_STD)
endif()

# OpenSSL/TLS
if(OPENSSL_AVAILABLE)
    target_include_directories(moonrt PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_compile_definitions(moonrt PRIVATE MOON_HAS_TLS)
    target_link_libraries(moonrt ${OPENSSL_LIBRARIES})
else()
    target_compile_definitions(moonrt PRIVATE MOON_NO_TLS)
endif()

target_link_libraries(moonrt ${PLATFORM_LIBS})

# ============================================================================
# LLVM compiler (optional)
# ============================================================================

option(BUILD_COMPILER "Build the MoonScript compiler (requires LLVM)" OFF)

if(BUILD_COMPILER)
    message(STATUS "Building MoonScript compiler")
    
    # Find LLVM
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    
    include_directories(${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
    
    # Compiler sources
    set(MOONC_SOURCES
        ${FRONTEND_SRC_DIR}/lexer.cpp
        ${FRONTEND_SRC_DIR}/parser.cpp
        ${FRONTEND_SRC_DIR}/alias_loader.cpp
        ${LLVM_SRC_DIR}/llvm_codegen.cpp
        ${LLVM_SRC_DIR}/moonc_llvm.cpp
    )
    
    add_executable(moonc ${MOONC_SOURCES})
    
    target_include_directories(moonc PRIVATE
        ${LLVM_SRC_DIR}
        ${SRC_DIR}
        ${FRONTEND_SRC_DIR}
    )
    
    # LLVM libs
    llvm_map_components_to_libnames(LLVM_LIBS
        core
        support
        irreader
        passes
        executionengine
        orcjit
        native
        x86codegen
        x86asmparser
        x86desc
        x86info
    )
    
    target_link_libraries(moonc
        moonrt
        ${LLVM_LIBS}
        ${PLATFORM_LIBS}
    )
endif()

# ============================================================================
# Install
# ============================================================================

install(TARGETS moonrt
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES
    ${LLVM_SRC_DIR}/moonrt.h
    ${LLVM_SRC_DIR}/moonrt_platform.h
    ${LLVM_SRC_DIR}/moonrt_core.h
    DESTINATION include/moonscript
)

if(BUILD_COMPILER)
    install(TARGETS moonc RUNTIME DESTINATION bin)
endif()

# Install stdlib
install(DIRECTORY ${SRC_DIR}/stdlib/
    DESTINATION share/moonscript/stdlib
    FILES_MATCHING PATTERN "*.moon"
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== MoonScript Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  GUI:     ${ENABLE_GUI}")
message(STATUS "  PCRE2:   ${PCRE2_AVAILABLE}")
message(STATUS "  TLS:     ${OPENSSL_AVAILABLE}")
message(STATUS "  Network: ${ENABLE_NETWORK}")
message(STATUS "  DLL:     ${ENABLE_DLL}")
message(STATUS "  JSON:    ${ENABLE_JSON}")
message(STATUS "")
message(STATUS "Build compiler: ${BUILD_COMPILER}")
message(STATUS "")
