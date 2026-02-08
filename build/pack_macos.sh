#!/bin/bash
# MoonLang macOS pack script
# Packs build output into moonlang_macos.tar.gz
#
# Usage:
#   Build first: build_macos.sh --compiler
#   Then run: ./pack_macos.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/macos_build"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_FILE="$SCRIPT_DIR/moonlang_macos.tar.gz"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo_info() { echo -e "${GREEN}[INFO]${NC} $1"; }

echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  MoonLang macOS Pack${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check build output
if [ ! -f "$BUILD_DIR/moonc" ]; then
    echo "Error: moonc not found. Run build_macos.sh --compiler first."
    exit 1
fi

if [ ! -f "$BUILD_DIR/libmoonrt.a" ]; then
    echo "Error: libmoonrt.a not found"
    exit 1
fi

# Temp pack dir
PACK_DIR=$(mktemp -d)
PACK_ROOT="$PACK_DIR/moonlang"
mkdir -p "$PACK_ROOT/bin"
mkdir -p "$PACK_ROOT/lib"
mkdir -p "$PACK_ROOT/stdlib"

echo_info "Copying files..."

cp "$BUILD_DIR/moonc" "$PACK_ROOT/bin/"
[ -f "$BUILD_DIR/mpkg" ] && cp "$BUILD_DIR/mpkg" "$PACK_ROOT/bin/"

cp "$BUILD_DIR/libmoonrt.a" "$PACK_ROOT/lib/"

[ -f "$BUILD_DIR/libpcre2.a" ] && cp "$BUILD_DIR/libpcre2.a" "$PACK_ROOT/lib/"

if [ -d "$PROJECT_DIR/stdlib" ]; then
    cp -r "$PROJECT_DIR/stdlib/"*.moon "$PACK_ROOT/stdlib/" 2>/dev/null || true
fi

if [ -d "$BUILD_DIR/stdlib" ]; then
    cp -r "$BUILD_DIR/stdlib/"*.moon "$PACK_ROOT/stdlib/" 2>/dev/null || true
fi

# Version info
echo "MoonLang for macOS" > "$PACK_ROOT/VERSION"
echo "Build date: $(date '+%Y-%m-%d %H:%M:%S')" >> "$PACK_ROOT/VERSION"
echo "Architecture: $(uname -m)" >> "$PACK_ROOT/VERSION"

# README
cat > "$PACK_ROOT/README.txt" << 'EOF'
MoonLang for macOS
==================

Install:
  1. Extract to any directory
  2. Add bin to PATH
  3. Set MOONPATH to stdlib directory

Example:
  export PATH="$HOME/.moonlang/bin:$PATH"
  export MOONPATH="$HOME/.moonlang/stdlib"

Usage:
  moonc hello.moon -o hello
  ./hello

Docs: https://greenteng.com/moonlang
EOF

echo_info "Creating archive..."
cd "$PACK_DIR"
tar -czf "$OUTPUT_FILE" moonlang

rm -rf "$PACK_DIR"

SIZE=$(ls -lh "$OUTPUT_FILE" | awk '{print $5}')
echo ""
echo_info "Pack complete!"
echo ""
echo "Output: $OUTPUT_FILE"
echo "Size: $SIZE"
echo ""
echo "Contents:"
echo "  - bin/moonc       (compiler)"
[ -f "$BUILD_DIR/mpkg" ] && echo "  - bin/mpkg        (package manager)"
echo "  - lib/libmoonrt.a (runtime)"
[ -f "$BUILD_DIR/libpcre2.a" ] && echo "  - lib/libpcre2.a  (PCRE2)"
echo "  - stdlib/         (standard library)"
echo ""
echo "Users can install with:"
echo "  curl -fsSL https://dl.greenteng.com/install_macos.sh | bash"
echo ""
