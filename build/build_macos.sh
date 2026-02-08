#!/bin/bash
# MoonScript macOS build script
# Copyright (c) 2026 greenteng.com
#
# Usage:
#   ./build_macos.sh              # Build runtime library only
#   ./build_macos.sh --compiler   # Build runtime and compiler
#   ./build_macos.sh --no-gui     # Build without GUI
#   ./build_macos.sh --compiler --no-gui  # Build compiler without GUI
#   ./build_macos.sh --clean      # Clean build directory
#   ./build_macos.sh --install    # Install to system

set -e

# Global options
ENABLE_GUI=true
ENABLE_TLS=true

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/macos_build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

echo_blue() {
    echo -e "${BLUE}$1${NC}"
}

# ============================================================================
# Detect macOS architecture
# ============================================================================

detect_arch() {
    local arch=$(uname -m)
    if [ "$arch" = "arm64" ]; then
        echo "Apple Silicon (ARM64)"
        HOMEBREW_PREFIX="/opt/homebrew"
    else
        echo "Intel (x86_64)"
        HOMEBREW_PREFIX="/usr/local"
    fi
}

# ============================================================================
# Check dependencies
# ============================================================================

check_dependencies() {
    echo_info "Checking build dependencies..."
    
    local missing=()
    
    # Xcode Command Line Tools
    if ! xcode-select -p &> /dev/null; then
        echo_error "Xcode Command Line Tools required"
        echo ""
        echo "Run: xcode-select --install"
        echo ""
        exit 1
    fi
    echo_info "✓ Xcode Command Line Tools installed"
    
    # Compiler
    if ! command -v clang++ &> /dev/null; then
        missing+=("clang++ (from Xcode Command Line Tools)")
    else
        echo_info "✓ clang++ installed"
    fi
    
    # CMake
    if ! command -v cmake &> /dev/null; then
        missing+=("cmake (brew install cmake)")
    else
        echo_info "✓ CMake installed ($(cmake --version | head -n1))"
    fi
    
    # Homebrew (optional but recommended)
    if ! command -v brew &> /dev/null; then
        echo_warn "Homebrew not installed - some features may be unavailable"
        echo_warn "Install: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    else
        echo_info "✓ Homebrew installed"
    fi
    
    # OpenSSL (TLS)
    if [ "$ENABLE_TLS" = true ]; then
        local openssl_found=false
        
        if [ -f "$HOMEBREW_PREFIX/opt/openssl@3/include/openssl/ssl.h" ]; then
            openssl_found=true
            echo_info "✓ OpenSSL installed ($HOMEBREW_PREFIX/opt/openssl@3)"
        elif [ -f "$HOMEBREW_PREFIX/opt/openssl/include/openssl/ssl.h" ]; then
            openssl_found=true
            echo_info "✓ OpenSSL installed ($HOMEBREW_PREFIX/opt/openssl)"
        fi
        
        if [ "$openssl_found" = false ]; then
            echo_warn "OpenSSL not found - TLS support disabled"
            echo_warn "Install: brew install openssl@3"
            ENABLE_TLS=false
        fi
    fi
    
    # macOS frameworks (system)
    if [ "$ENABLE_GUI" = true ]; then
        echo_info "✓ Cocoa/AppKit (system)"
        echo_info "✓ WebKit (system)"
    fi
    
    if [ ${#missing[@]} -gt 0 ]; then
        echo_error "Missing dependencies:"
        for dep in "${missing[@]}"; do
            echo "  - $dep"
        done
        echo ""
        echo "Install missing dependencies and retry"
        exit 1
    fi
    
    echo_info "All dependencies satisfied"
}

# ============================================================================
# Check LLVM (for compiler build)
# ============================================================================

check_llvm() {
    echo_info "Checking LLVM..."
    
    local llvm_config=""
    
    if [ -f "$HOMEBREW_PREFIX/opt/llvm/bin/llvm-config" ]; then
        llvm_config="$HOMEBREW_PREFIX/opt/llvm/bin/llvm-config"
    elif command -v llvm-config &> /dev/null; then
        llvm_config="llvm-config"
    fi
    
    if [ -z "$llvm_config" ]; then
        echo_warn "LLVM not installed - cannot build compiler"
        echo ""
        echo "Install LLVM:"
        echo "  brew install llvm"
        echo ""
        echo "Add to PATH:"
        echo "  export PATH=\"$HOMEBREW_PREFIX/opt/llvm/bin:\$PATH\""
        echo ""
        return 1
    fi
    
    local llvm_version=$($llvm_config --version)
    echo_info "✓ Found LLVM $llvm_version"
    
    export LLVM_DIR="$($llvm_config --prefix)"
    export PATH="$LLVM_DIR/bin:$PATH"
    
    return 0
}

# ============================================================================
# Check/download PCRE2
# ============================================================================

check_pcre2() {
    local PCRE2_PATHS=(
        "$PROJECT_DIR/lib/pcre2/src"
        "$SCRIPT_DIR/../lib/pcre2/src"
        "$(cd "$SCRIPT_DIR/.." && pwd)/lib/pcre2/src"
    )
    
    echo_info "Checking PCRE2..."
    
    for pcre2_src in "${PCRE2_PATHS[@]}"; do
        if [ -d "$(dirname "$pcre2_src")" ]; then
            pcre2_src="$(cd "$(dirname "$pcre2_src")" 2>/dev/null && pwd)/$(basename "$pcre2_src")"
        fi
        
        if [ -f "$pcre2_src/pcre2.h" ]; then
            echo_info "✓ PCRE2 found ($pcre2_src)"
            return 0
        fi
    done
    
    echo_info "PCRE2 not found, downloading..."
    
    local PCRE2_DIR="$PROJECT_DIR/lib/pcre2"
    local PCRE2_SRC="$PCRE2_DIR/src"
    local PCRE2_VERSION="10.42"
    local PCRE2_URL="https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_VERSION}/pcre2-${PCRE2_VERSION}.tar.gz"
    
    mkdir -p "$PCRE2_SRC"
    
    local TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    if curl -LO "$PCRE2_URL" 2>/dev/null; then
        tar -xzf "pcre2-${PCRE2_VERSION}.tar.gz"
        cp -r "pcre2-${PCRE2_VERSION}/src/"* "$PCRE2_SRC/"
        echo_info "✓ PCRE2 ${PCRE2_VERSION} downloaded"
    else
        echo_warn "PCRE2 download failed, will use std::regex"
    fi
    
    cd "$SCRIPT_DIR"
    rm -rf "$TEMP_DIR"
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
    
    echo ""
    echo_blue "========================================"
    echo_blue "  Building MoonScript for macOS"
    echo_blue "========================================"
    echo ""
    
    generate_version
    check_pcre2
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    local cmake_args="-DCMAKE_BUILD_TYPE=Release"
    
    # GUI
    if [ "$ENABLE_GUI" = true ]; then
        cmake_args="$cmake_args -DENABLE_GUI=ON"
        echo_info "GUI support: enabled (AppKit + WKWebView)"
    else
        cmake_args="$cmake_args -DENABLE_GUI=OFF"
        echo_info "GUI support: disabled"
    fi
    
    # TLS
    if [ "$ENABLE_TLS" = true ]; then
        cmake_args="$cmake_args -DENABLE_TLS=ON"
        echo_info "TLS support: enabled (OpenSSL)"
    else
        cmake_args="$cmake_args -DENABLE_TLS=OFF"
        echo_info "TLS support: disabled"
    fi
    
    # Compiler
    if [ "$build_compiler" = "true" ]; then
        if check_llvm; then
            cmake_args="$cmake_args -DBUILD_COMPILER=ON"
            cmake_args="$cmake_args -DLLVM_DIR=$LLVM_DIR"
            echo_info "Compiler: will build"
        else
            echo_warn "Skipping compiler (LLVM unavailable)"
        fi
    fi
    
    echo ""
    echo_info "Running CMake..."
    cmake $cmake_args "$SCRIPT_DIR"
    
    echo ""
    echo_info "Compiling..."
    local cores=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make -j$cores
    
    echo ""
    echo_blue "========================================"
    echo_blue "  Build complete!"
    echo_blue "========================================"
    echo ""
    echo "Output:"
    echo "  - $BUILD_DIR/libmoonrt.a  (runtime library)"
    
    if [ -f "$BUILD_DIR/moonc" ]; then
        echo "  - $BUILD_DIR/moonc       (compiler)"
    fi
    
    if [ -f "$BUILD_DIR/mpkg" ]; then
        echo "  - $BUILD_DIR/mpkg        (package manager)"
    fi
    
    echo ""
    echo_info "Usage:"
    echo "  cd $BUILD_DIR"
    echo "  ./moonc your_program.moon -o your_program"
    echo "  ./your_program"
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
    
    local install_prefix="/usr/local"
    
    if [ -f "$BUILD_DIR/moonc" ]; then
        sudo cp "$BUILD_DIR/moonc" "$install_prefix/bin/"
        echo_info "Installed: $install_prefix/bin/moonc"
    fi
    
    if [ -f "$BUILD_DIR/mpkg" ]; then
        sudo cp "$BUILD_DIR/mpkg" "$install_prefix/bin/"
        echo_info "Installed: $install_prefix/bin/mpkg"
    fi
    
    if [ -f "$BUILD_DIR/libmoonrt.a" ]; then
        sudo cp "$BUILD_DIR/libmoonrt.a" "$install_prefix/lib/"
        echo_info "Installed: $install_prefix/lib/libmoonrt.a"
    fi
    
    echo ""
    echo_info "Install complete!"
    echo_info "You can now use 'moonc' from anywhere"
}

# ============================================================================
# Help
# ============================================================================

show_help() {
    echo "MoonScript macOS build script"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  (none)      Build runtime library only"
    echo "  --compiler  Build runtime and compiler (requires LLVM)"
    echo "  --no-gui    Disable GUI (no Cocoa/WebKit)"
    echo "  --no-tls    Disable TLS (no OpenSSL)"
    echo "  --clean     Clean build directory"
    echo "  --install   Install to system (requires sudo)"
    echo "  --help      Show this help"
    echo ""
    echo "Dependencies:"
    echo "  # Xcode Command Line Tools"
    echo "  xcode-select --install"
    echo ""
    echo "  # Homebrew"
    echo "  brew install cmake llvm openssl@3"
    echo ""
    echo "  # Add LLVM to PATH (~/.zshrc or ~/.bash_profile)"
    echo "  export PATH=\"\$(brew --prefix llvm)/bin:\$PATH\""
    echo ""
    echo "Examples:"
    echo "  ./build_macos.sh --compiler         # Full build with GUI"
    echo "  ./build_macos.sh --compiler --no-gui  # Server build"
    echo "  ./build_macos.sh --install          # Install to system"
    echo ""
}

# ============================================================================
# Main
# ============================================================================

main() {
    echo ""
    echo_blue "==================================="
    echo_blue "  MoonScript macOS Build Script"
    echo_blue "==================================="
    echo ""
    
    echo_info "Architecture: $(detect_arch)"
    echo ""
    
    local build_compiler=false
    local do_clean=false
    local do_install=false
    local show_help_flag=false
    
    for arg in "$@"; do
        case "$arg" in
            --no-gui)
                ENABLE_GUI=false
                ;;
            --no-tls)
                ENABLE_TLS=false
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
                show_help_flag=true
                ;;
        esac
    done
    
    if [ "$show_help_flag" = true ]; then
        show_help
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
