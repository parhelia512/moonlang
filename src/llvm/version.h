// Auto-generated from version.json - DO NOT EDIT MANUALLY
// Run rebuild_all.bat after modifying version.json
// Copyright (c) 2026 greenteng.com

#ifndef MOONLANG_VERSION_H
#define MOONLANG_VERSION_H

// ============================================================================
// VERSION - Auto-generated from version.json
// ============================================================================

#define MOONLANG_VERSION_MAJOR  1
#define MOONLANG_VERSION_MINOR  4
#define MOONLANG_VERSION_PATCH  0
#define MOONLANG_VERSION_BUILD  0

// ============================================================================
// DERIVED VERSION MACROS - DO NOT MODIFY
// ============================================================================

#define MOONLANG_STRINGIFY2(x) #x
#define MOONLANG_STRINGIFY(x) MOONLANG_STRINGIFY2(x)

#define MOONLANG_VERSION_STRING \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MAJOR) "." \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MINOR) "." \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_PATCH)

#define MOONLANG_VERSION_STRING_FULL \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MAJOR) "." \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MINOR) "." \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_PATCH) "." \
    MOONLANG_STRINGIFY(MOONLANG_VERSION_BUILD)

#define MOONLANG_VERSION_INT \
    ((MOONLANG_VERSION_MAJOR * 10000) + (MOONLANG_VERSION_MINOR * 100) + MOONLANG_VERSION_PATCH)

#define MOONLANG_VERSION_RC \
    MOONLANG_VERSION_MAJOR,MOONLANG_VERSION_MINOR,MOONLANG_VERSION_PATCH,MOONLANG_VERSION_BUILD

#define MOONLANG_COPYRIGHT "Copyright (c) 2026 greenteng.com"
#define MOONLANG_COMPANY "greenteng.com"
#define MOONLANG_PRODUCT_NAME "MoonLang"

#endif // MOONLANG_VERSION_H
