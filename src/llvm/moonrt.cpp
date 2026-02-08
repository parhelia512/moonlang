// MoonLang Runtime Library - Unified Entry Point
// Copyright (c) 2026 greenteng.com
//
// This file aggregates all runtime modules into a single compilation unit.
// The runtime is modular and can be configured via MOON_NO_XXX macros.
//
// Module structure:
//   moonrt_core.cpp     - Type system, memory, reference counting, OOP
//   moonrt_math.cpp     - Arithmetic, comparison, math functions
//   moonrt_string.cpp   - String operations
//   moonrt_list.cpp     - List operations
//   moonrt_dict.cpp     - Dictionary operations
//   moonrt_builtin.cpp  - Built-in functions (print, input, etc.)
//   moonrt_io.cpp       - File I/O, paths, date/time
//   moonrt_json.cpp     - JSON encoding/decoding (conditional)
//   moonrt_network.cpp  - TCP/UDP networking (conditional)
//   moonrt_dll.cpp      - DLL/shared library loading (conditional)
//   moonrt_regex.cpp    - Regular expressions using PCRE2 (conditional)
//   moonrt_tls.cpp      - TLS/SSL support using OpenSSL (conditional)
//   moonrt_async.cpp    - Async/await support (separate)
//   moonrt_channel.cpp  - Go-style channels (separate)
//   moonrt_gui.cpp      - GUI support (separate)

// ============================================================================
// Include all module implementations
// ============================================================================

#include "moonrt_core.cpp"
#include "moonrt_bigint.cpp"
#include "moonrt_math.cpp"
#include "moonrt_string.cpp"
#include "moonrt_list.cpp"
#include "moonrt_dict.cpp"
#include "moonrt_builtin.cpp"
#include "moonrt_io.cpp"
#include "moonrt_json.cpp"
#include "moonrt_network.cpp"
#include "moonrt_dll.cpp"

// Note: moonrt_regex.cpp, moonrt_async.cpp, moonrt_channel.cpp, moonrt_gui.cpp,
// and moonrt_tls.cpp are compiled separately to allow for conditional compilation
// and to avoid pulling in dependencies (like PCRE2, WebView2, OpenSSL) unless needed.
