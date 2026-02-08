#!/bin/bash
# Generate version.h from version.json
# Usage: ./gen_version.sh
# Copyright (c) 2026 greenteng.com

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
JSON_PATH="$ROOT_DIR/version.json"
HEADER_PATH="$ROOT_DIR/src/llvm/version.h"

# Check if version.json exists
if [ ! -f "$JSON_PATH" ]; then
    echo "Error: version.json not found at $JSON_PATH"
    exit 1
fi

# Parse JSON using grep/sed (works without jq)
parse_json_value() {
    local key=$1
    grep "\"$key\"" "$JSON_PATH" | sed 's/.*: *"\?\([^",}]*\)"\?.*/\1/' | tr -d ' '
}

MAJOR=$(parse_json_value "major")
MINOR=$(parse_json_value "minor")
PATCH=$(parse_json_value "patch")
BUILD=$(parse_json_value "build")
COMPANY=$(grep '"company"' "$JSON_PATH" | sed 's/.*: *"\([^"]*\)".*/\1/')
COPYRIGHT=$(grep '"copyright"' "$JSON_PATH" | sed 's/.*: *"\([^"]*\)".*/\1/')
PRODUCT=$(grep '"product"' "$JSON_PATH" | sed 's/.*: *"\([^"]*\)".*/\1/')

# Create header content
cat > "$HEADER_PATH" << EOF
// Auto-generated from version.json - DO NOT EDIT MANUALLY
// Run rebuild_all.bat (Windows) or build_*.sh (Linux/macOS) after modifying version.json
// Copyright (c) 2026 greenteng.com

#ifndef MOONLANG_VERSION_H
#define MOONLANG_VERSION_H

// ============================================================================
// VERSION - Auto-generated from version.json
// ============================================================================

#define MOONLANG_VERSION_MAJOR  $MAJOR
#define MOONLANG_VERSION_MINOR  $MINOR
#define MOONLANG_VERSION_PATCH  $PATCH
#define MOONLANG_VERSION_BUILD  $BUILD

// ============================================================================
// DERIVED VERSION MACROS - DO NOT MODIFY
// ============================================================================

#define MOONLANG_STRINGIFY2(x) #x
#define MOONLANG_STRINGIFY(x) MOONLANG_STRINGIFY2(x)

#define MOONLANG_VERSION_STRING \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MAJOR) "." \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MINOR) "." \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_PATCH)

#define MOONLANG_VERSION_STRING_FULL \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MAJOR) "." \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_MINOR) "." \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_PATCH) "." \\
    MOONLANG_STRINGIFY(MOONLANG_VERSION_BUILD)

#define MOONLANG_VERSION_INT \\
    ((MOONLANG_VERSION_MAJOR * 10000) + (MOONLANG_VERSION_MINOR * 100) + MOONLANG_VERSION_PATCH)

#define MOONLANG_VERSION_RC \\
    MOONLANG_VERSION_MAJOR,MOONLANG_VERSION_MINOR,MOONLANG_VERSION_PATCH,MOONLANG_VERSION_BUILD

#define MOONLANG_COPYRIGHT "$COPYRIGHT"
#define MOONLANG_COMPANY "$COMPANY"
#define MOONLANG_PRODUCT_NAME "$PRODUCT"

#endif // MOONLANG_VERSION_H
EOF

echo "Generated version.h: v${MAJOR}.${MINOR}.${PATCH}"
