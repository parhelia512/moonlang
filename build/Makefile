# MoonScript Linux Makefile
# Simple Makefile for Linux build
# Copyright (c) 2026 greenteng.com
#
# Usage:
#   make              # Build runtime library
#   make moonc        # Build compiler (requires LLVM)
#   make clean        # Clean
#   make install      # Install

# Compiler
CXX ?= g++
CC ?= gcc
AR ?= ar

# Directories
SRC_DIR = ..
LLVM_DIR = $(SRC_DIR)/llvm
BUILD_DIR = linux_build

# Flags
CXXFLAGS = -std=c++17 -O2 -DNDEBUG -fPIC
CFLAGS = -O2 -DNDEBUG -fPIC

# GTK and WebKitGTK (4.0 and 4.1)
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0 2>/dev/null)

# WebKit (prefer 4.1)
WEBKIT_PKG := $(shell pkg-config --exists webkit2gtk-4.1 2>/dev/null && echo webkit2gtk-4.1 || echo webkit2gtk-4.0)
WEBKIT_CFLAGS = $(shell pkg-config --cflags $(WEBKIT_PKG) 2>/dev/null)
WEBKIT_LIBS = $(shell pkg-config --libs $(WEBKIT_PKG) 2>/dev/null)

# AppIndicator (optional)
APPINDICATOR_CFLAGS = $(shell pkg-config --cflags appindicator3-0.1 2>/dev/null)
APPINDICATOR_LIBS = $(shell pkg-config --libs appindicator3-0.1 2>/dev/null)
ifneq ($(APPINDICATOR_LIBS),)
    CXXFLAGS += -DHAVE_APPINDICATOR
endif

# Includes
INCLUDES = -I$(SRC_DIR) -I$(LLVM_DIR) $(GTK_CFLAGS) $(WEBKIT_CFLAGS) $(APPINDICATOR_CFLAGS)

# Libs
LIBS = -lpthread -ldl $(GTK_LIBS) $(WEBKIT_LIBS) $(APPINDICATOR_LIBS)

# Runtime sources
MOONRT_SRCS = \
    $(LLVM_DIR)/moonrt.cpp \
    $(LLVM_DIR)/moonrt_async.cpp \
    $(LLVM_DIR)/moonrt_channel.cpp \
    $(LLVM_DIR)/moonrt_gui_linux.cpp

MOONRT_OBJS = $(patsubst $(LLVM_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(MOONRT_SRCS))

# Default
.PHONY: all
all: check-deps $(BUILD_DIR) libmoonrt.a
	@echo ""
	@echo "=== Build complete ==="
	@echo "Output: libmoonrt.a"
	@echo ""

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists gtk+-3.0 || (echo "Error: missing libgtk-3-dev"; exit 1)
	@(pkg-config --exists webkit2gtk-4.1 || pkg-config --exists webkit2gtk-4.0) || (echo "Error: missing libwebkit2gtk-4.1-dev or libwebkit2gtk-4.0-dev"; exit 1)
	@echo "Dependencies OK (using $(WEBKIT_PKG))"

# Runtime library
libmoonrt.a: $(MOONRT_OBJS)
	$(AR) rcs $@ $^
	@echo "Created static library: libmoonrt.a"

# .cpp -> .o
$(BUILD_DIR)/%.o: $(LLVM_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================================================
# Compiler (requires LLVM)
# ============================================================================

LLVM_CONFIG ?= llvm-config
LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support native x86 2>/dev/null)
LLVM_SYSLIBS = $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)

# Compiler sources
MOONC_SRCS = \
    $(SRC_DIR)/lexer.cpp \
    $(SRC_DIR)/parser.cpp \
    $(LLVM_DIR)/llvm_codegen.cpp \
    $(LLVM_DIR)/moonc_llvm.cpp

MOONC_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/moonc_%.o,$(MOONC_SRCS))
MOONC_OBJS := $(patsubst $(LLVM_DIR)/%.cpp,$(BUILD_DIR)/moonc_%.o,$(MOONC_OBJS))

.PHONY: moonc
moonc: check-llvm libmoonrt.a $(MOONC_OBJS)
	$(CXX) $(MOONC_OBJS) libmoonrt.a $(LLVM_LDFLAGS) $(LLVM_LIBS) $(LLVM_SYSLIBS) $(LIBS) -o moonc
	@echo ""
	@echo "=== Compiler build complete ==="
	@echo "Output: moonc"
	@echo ""

.PHONY: check-llvm
check-llvm:
	@which $(LLVM_CONFIG) > /dev/null || (echo "Error: llvm-config not found, install llvm-dev"; exit 1)
	@echo "Found LLVM: $$($(LLVM_CONFIG) --version)"

$(BUILD_DIR)/moonc_lexer.o: $(SRC_DIR)/lexer.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/moonc_parser.o: $(SRC_DIR)/parser.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/moonc_llvm_codegen.o: $(LLVM_DIR)/llvm_codegen.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/moonc_moonc_llvm.o: $(LLVM_DIR)/moonc_llvm.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================================================
# Clean and install
# ============================================================================

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) libmoonrt.a moonc
	@echo "Clean done"

.PHONY: install
install: all
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/moonscript
	install -d $(DESTDIR)/usr/local/share/moonscript/stdlib
	install -m 644 libmoonrt.a $(DESTDIR)/usr/local/lib/
	install -m 644 $(LLVM_DIR)/moonrt.h $(DESTDIR)/usr/local/include/moonscript/
	install -m 644 $(LLVM_DIR)/moonrt_platform.h $(DESTDIR)/usr/local/include/moonscript/
	cp -r $(SRC_DIR)/stdlib/*.moon $(DESTDIR)/usr/local/share/moonscript/stdlib/
	@if [ -f moonc ]; then \
		install -d $(DESTDIR)/usr/local/bin; \
		install -m 755 moonc $(DESTDIR)/usr/local/bin/; \
	fi
	@echo "Install complete"

.PHONY: help
help:
	@echo "MoonScript Linux Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build runtime library (default)"
	@echo "  moonc     - Build compiler (requires LLVM)"
	@echo "  clean     - Clean build files"
	@echo "  install   - Install to system"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Runtime only"
	@echo "  make moonc        # Compiler"
	@echo "  sudo make install # Install"
