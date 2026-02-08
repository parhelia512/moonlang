# PCRE2 Build Configuration for MoonScript
# This builds PCRE2 as a static library for the MoonScript runtime
# Supports PCRE2 10.45+ with additional source files

cmake_minimum_required(VERSION 3.10)
project(pcre2 C)

# PCRE2 Configuration
set(PCRE2_CODE_UNIT_WIDTH 8)
add_definitions(-DPCRE2_CODE_UNIT_WIDTH=8)
add_definitions(-DPCRE2_STATIC)
add_definitions(-DHAVE_CONFIG_H)

# Enable Unicode support
add_definitions(-DSUPPORT_UNICODE)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Source files for PCRE2 10.45+ (includes new modular files)
set(PCRE2_SOURCES
    src/pcre2_auto_possess.c
    src/pcre2_chartables.c
    src/pcre2_chkdint.c
    src/pcre2_compile.c
    src/pcre2_compile_class.c
    src/pcre2_compile_cgroup.c
    src/pcre2_config.c
    src/pcre2_context.c
    src/pcre2_convert.c
    src/pcre2_dfa_match.c
    src/pcre2_error.c
    src/pcre2_extuni.c
    src/pcre2_find_bracket.c
    src/pcre2_jit_compile.c
    src/pcre2_maketables.c
    src/pcre2_match.c
    src/pcre2_match_data.c
    src/pcre2_match_next.c
    src/pcre2_newline.c
    src/pcre2_ord2utf.c
    src/pcre2_pattern_info.c
    src/pcre2_script_run.c
    src/pcre2_serialize.c
    src/pcre2_string_utils.c
    src/pcre2_study.c
    src/pcre2_substitute.c
    src/pcre2_substring.c
    src/pcre2_tables.c
    src/pcre2_ucd.c
    src/pcre2_valid_utf.c
    src/pcre2_xclass.c
)

# Check if source files exist
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/pcre2.h")
    # Build static library
    add_library(pcre2 STATIC ${PCRE2_SOURCES})
    
    # Set output name
    set_target_properties(pcre2 PROPERTIES
        OUTPUT_NAME "pcre2-8"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    
    # Export include directory
    target_include_directories(pcre2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
else()
    message(WARNING "PCRE2 source not found. Run download_pcre2.bat first.")
endif()
