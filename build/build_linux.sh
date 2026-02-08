#!/bin/bash
# MoonScript Linux build script
# Copyright (c) 2026 greenteng.com
#
# Usage:
#   ./build_linux.sh              # Build runtime library only
#   ./build_linux.sh --compiler   # Build runtime and compiler
#   ./build_linux.sh --no-gui     # Build without GUI (recommended for servers)
#   ./build_linux.sh --compiler --no-gui  # Build compiler without GUI
#   ./build_linux.sh --clean     # Clean build directory
#   ./build_linux.sh --install   # Install to system

set -e

# Global options
ENABLE_GUI=true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/linux_build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ============================================================================
# Check dependencies
# ============================================================================

check_dependencies() {
    echo_info "Checking build dependencies..."
    
    local missing=()
    
    # Check compiler
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        missing+=("g++ or clang++ (build-essential)")
    fi
    
    # Check CMake
    if ! command -v cmake &> /dev/null; then
        missing+=("cmake")
    fi
    
    # Check pkg-config (GUI mode only)
    if [ "$ENABLE_GUI" = true ]; then
        if ! command -v pkg-config &> /dev/null; then
            missing+=("pkg-config")
        fi
        
        # Check GTK3
        if ! pkg-config --exists gtk+-3.0 2>/dev/null; then
            missing+=("libgtk-3-dev")
        fi
        
        # Check WebKitGTK (4.0 and 4.1)
        if pkg-config --exists webkit2gtk-4.1 2>/dev/null; then
            WEBKIT_PKG="webkit2gtk-4.1"
        elif pkg-config --exists webkit2gtk-4.0 2>/dev/null; then
            WEBKIT_PKG="webkit2gtk-4.0"
        else
            missing+=("libwebkit2gtk-4.1-dev or libwebkit2gtk-4.0-dev")
        fi
        
        # Optional: AppIndicator
        if ! pkg-config --exists appindicator3-0.1 2>/dev/null; then
            echo_warn "libappindicator3-dev not installed - system tray unavailable"
        fi
    else
        echo_info "GUI support disabled - skipping GTK/WebKit check"
    fi
    
    if [ ${#missing[@]} -gt 0 ]; then
        echo_error "Missing dependencies:"
        for dep in "${missing[@]}"; do
            echo "  - $dep"
        done
        echo ""
        if [ "$ENABLE_GUI" = true ]; then
            echo "Install with:"
            echo ""
            echo "  # Ubuntu/Debian:"
            echo "  sudo apt install build-essential cmake pkg-config \\"
            echo "      libgtk-3-dev libwebkit2gtk-4.0-dev libappindicator3-dev"
            echo ""
            echo "  # Or build without GUI:"
            echo "  ./build_linux.sh --compiler --no-gui"
            echo ""
        else
            echo "Install with:"
            echo "  sudo apt install build-essential cmake"
            echo ""
        fi
        exit 1
    fi
    
    echo_info "All dependencies satisfied"
}

# ============================================================================
# Check LLVM (optional for compiler build)
# ============================================================================

check_llvm() {
    echo_info "Checking LLVM..."
    
    if ! command -v llvm-config &> /dev/null; then
        echo_warn "LLVM not installed - cannot build compiler"
        echo "Install: sudo apt install llvm-dev (Ubuntu) or sudo dnf install llvm-devel (Fedora)"
        return 1
    fi
    
    local llvm_version=$(llvm-config --version)
    echo_info "Found LLVM $llvm_version"
    return 0
}

# ============================================================================
# Clean
# ============================================================================

clean_build() {
    echo_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo_info "Clean done"
}

# ============================================================================
# Build
# ============================================================================

generate_version() {
    echo_info "Generating version.h from version.json..."
    
    local gen_script="$PROJECT_DIR/gen_version.sh"
    if [ -f "$gen_script" ]; then
        chmod +x "$gen_script"
        bash "$gen_script"
    else
        echo_warn "gen_version.sh not found, skipping version generation"
    fi
}

build_runtime() {
    local build_compiler=$1
    
    echo_info "Building MoonScript runtime library..."
    
    generate_version
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    local cmake_args="-DCMAKE_BUILD_TYPE=Release"
    
    # GUI option
    if [ "$ENABLE_GUI" = true ]; then
        cmake_args="$cmake_args -DENABLE_GUI=ON"
        echo_info "GUI support: enabled"
    else
        cmake_args="$cmake_args -DENABLE_GUI=OFF"
        echo_info "GUI support: disabled (no GTK/WebKit)"
    fi
    
    if [ "$build_compiler" = "true" ]; then
        if check_llvm; then
            cmake_args="$cmake_args -DBUILD_COMPILER=ON"
            echo_info "Will also build compiler"
        else
            echo_warn "Skipping compiler build"
        fi
    fi
    
    echo_info "Running CMake..."
    cmake $cmake_args "$SCRIPT_DIR"
    
    echo_info "Compiling..."
    local cores=$(nproc 2>/dev/null || echo 4)
    make -j$cores
    
    echo_info "Build complete!"
    
    echo ""
    echo "Output:"
    echo "  - $BUILD_DIR/libmoonrt.a  (runtime library)"
    
    if [ -f "$BUILD_DIR/moonc" ]; then
        echo "  - $BUILD_DIR/moonc       (compiler)"
        if [ "$ENABLE_GUI" = false ]; then
            echo ""
            echo_info "This compiler has no GUI; runs on any Linux system"
        fi
    fi
    echo ""
}

# ============================================================================
# Install
# ============================================================================

install_moonscript() {
    echo_info "Installing MoonScript..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        echo_error "Run build first"
        exit 1
    fi
    
    cd "$BUILD_DIR"
    sudo make install
    
    echo_info "Install complete!"
}

# ============================================================================
# Main
# ============================================================================

main() {
    echo ""
    echo "==================================="
    echo "  MoonScript Linux Build Script"
    echo "==================================="
    echo ""
    
    local build_compiler=false
    local do_clean=false
    local do_install=false
    local show_help=false
    
    for arg in "$@"; do
        case "$arg" in
            --no-gui)
                ENABLE_GUI=false
                ;;
            --compiler)
                build_compiler=true
                ;;
            --clean)
                do_clean=true
                ;;
            --install)
                do_install=true
                ;;
            --help|-h)
                show_help=true
                ;;
        esac
    done
    
    if [ "$show_help" = true ]; then
        echo "Usage: $0 [options]"
        echo ""
        echo "Options:"
        echo "  (none)      Build runtime library only"
        echo "  --compiler  Build runtime and compiler (requires LLVM)"
        echo "  --no-gui    Disable GUI (no GTK/WebKit, for servers)"
        echo "  --clean     Clean build directory"
        echo "  --install   Install to system (requires sudo)"
        echo "  --help      Show this help"
        echo ""
        echo "Examples:"
        echo "  ./build_linux.sh --compiler --no-gui   # Compiler without GUI"
        echo "  ./build_linux.sh --compiler            # Compiler with GUI"
        echo ""
        return
    fi
    
    if [ "$do_clean" = true ]; then
        clean_build
        return
    fi
    
    if [ "$do_install" = true ]; then
        install_moonscript
        return
    fi
    
    check_dependencies
    build_runtime "$build_compiler"
}

main "$@"
