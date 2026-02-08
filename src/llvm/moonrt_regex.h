// MoonLang Runtime - Regex Support
// Copyright (c) 2026 greenteng.com
//
// Regular expression support using PCRE2 or C++ std::regex as fallback

#ifndef MOONRT_REGEX_H
#define MOONRT_REGEX_H

#include "moonrt.h"
#include "moonrt_platform.h"

// Note: moonrt_regex.cpp uses moonrt_core.h internally for internal functions

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Regex Feature Flag
// ============================================================================

// Define MOON_NO_REGEX to disable regex support (for minimal builds)
#ifndef MOON_NO_REGEX
    #define MOON_HAS_REGEX 1
#endif

#ifdef MOON_HAS_REGEX

// ============================================================================
// Basic Regex Functions
// ============================================================================

// Check if pattern matches the entire string
// regex_match("hello123", "\\w+\\d+") -> true
MoonValue* moon_regex_match(MoonValue* str, MoonValue* pattern);

// Search for first match in string
// regex_search("test hello world", "\\w+") -> "test"
MoonValue* moon_regex_search(MoonValue* str, MoonValue* pattern);

// Check if pattern exists anywhere in string
// regex_test("hello world", "wor") -> true
MoonValue* moon_regex_test(MoonValue* str, MoonValue* pattern);

// ============================================================================
// Capture Groups
// ============================================================================

// Get all capture groups from first match
// regex_groups("2026-01-25", "(\\d+)-(\\d+)-(\\d+)")
// -> ["2026-01-25", "2026", "01", "25"]
MoonValue* moon_regex_groups(MoonValue* str, MoonValue* pattern);

// Get named capture groups as dictionary
// regex_named("John:25", "(?P<name>\\w+):(?P<age>\\d+)")
// -> {"0": "John:25", "name": "John", "age": "25"}
MoonValue* moon_regex_named(MoonValue* str, MoonValue* pattern);

// ============================================================================
// Global Matching
// ============================================================================

// Find all matches (non-overlapping)
// regex_find_all("a1 b2 c3", "\\w\\d") -> ["a1", "b2", "c3"]
MoonValue* moon_regex_find_all(MoonValue* str, MoonValue* pattern);

// Find all matches with groups
// regex_find_all_groups("a1 b2", "(\\w)(\\d)")
// -> [["a1", "a", "1"], ["b2", "b", "2"]]
MoonValue* moon_regex_find_all_groups(MoonValue* str, MoonValue* pattern);

// ============================================================================
// Replacement
// ============================================================================

// Replace first occurrence
// regex_replace("hello world", "\\w+", "X") -> "X world"
MoonValue* moon_regex_replace(MoonValue* str, MoonValue* pattern, MoonValue* replacement);

// Replace all occurrences
// regex_replace_all("a1 b2", "\\w\\d", "X") -> "X X"
MoonValue* moon_regex_replace_all(MoonValue* str, MoonValue* pattern, MoonValue* replacement);

// ============================================================================
// Splitting
// ============================================================================

// Split by regex pattern
// regex_split("a1b2c3", "\\d") -> ["a", "b", "c"]
MoonValue* moon_regex_split(MoonValue* str, MoonValue* pattern);

// Split with limit
// regex_split_n("a1b2c3d4", "\\d", 2) -> ["a", "b2c3d4"]
MoonValue* moon_regex_split_n(MoonValue* str, MoonValue* pattern, MoonValue* limit);

// ============================================================================
// Compiled Regex (Performance Optimization)
// ============================================================================

// Compile a regex pattern for reuse
// pattern = regex_compile("\\d+")
MoonValue* moon_regex_compile(MoonValue* pattern);

// Match using compiled pattern
MoonValue* moon_regex_match_compiled(MoonValue* compiled, MoonValue* str);

// Search using compiled pattern
MoonValue* moon_regex_search_compiled(MoonValue* compiled, MoonValue* str);

// Find all using compiled pattern
MoonValue* moon_regex_find_all_compiled(MoonValue* compiled, MoonValue* str);

// Replace using compiled pattern
MoonValue* moon_regex_replace_compiled(MoonValue* compiled, MoonValue* str, MoonValue* replacement);

// Free compiled pattern
void moon_regex_free(MoonValue* compiled);

// ============================================================================
// Utility Functions
// ============================================================================

// Escape special regex characters in string
// regex_escape("hello.world") -> "hello\\.world"
MoonValue* moon_regex_escape(MoonValue* str);

// Get last regex error message
MoonValue* moon_regex_error(void);

#endif // MOON_HAS_REGEX

#ifdef __cplusplus
}
#endif

#endif // MOONRT_REGEX_H
