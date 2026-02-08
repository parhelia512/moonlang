# Generate version.h from version.json
# Usage: powershell -ExecutionPolicy Bypass -File gen_version.ps1

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$jsonPath = Join-Path $rootDir "version.json"
$headerPath = Join-Path $rootDir "src\llvm\version.h"

# Read JSON
$json = Get-Content -Raw $jsonPath | ConvertFrom-Json

# Generate header content
$content = @"
// Auto-generated from version.json - DO NOT EDIT MANUALLY
// Run rebuild_all.bat after modifying version.json
// Copyright (c) 2026 greenteng.com

#ifndef MOONLANG_VERSION_H
#define MOONLANG_VERSION_H

// ============================================================================
// VERSION - Auto-generated from version.json
// ============================================================================

#define MOONLANG_VERSION_MAJOR  $($json.major)
#define MOONLANG_VERSION_MINOR  $($json.minor)
#define MOONLANG_VERSION_PATCH  $($json.patch)
#define MOONLANG_VERSION_BUILD  $($json.build)

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

#define MOONLANG_COPYRIGHT "$($json.copyright)"
#define MOONLANG_COMPANY "$($json.company)"
#define MOONLANG_PRODUCT_NAME "$($json.product)"

#endif // MOONLANG_VERSION_H
"@

# Write header file
$content | Out-File -Encoding ASCII $headerPath

Write-Host "Generated version.h: v$($json.major).$($json.minor).$($json.patch)"
