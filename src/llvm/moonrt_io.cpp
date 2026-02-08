// MoonLang Runtime - I/O Module
// Copyright (c) 2026 greenteng.com
//
// File operations, path functions, and date/time functions.

#include "moonrt_core.h"

#ifdef MOON_PLATFORM_WINDOWS
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <limits.h>
#endif

// ============================================================================
// File Operations
// ============================================================================

MoonValue* moon_read_file(MoonValue* path) {
    if (!moon_is_string(path)) return moon_null();
    
    FILE* file = fopen(path->data.strVal, "rb");
    if (!file) return moon_null();
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Use string with header to store binary data length correctly
    char* content = moon_str_with_capacity(NULL, 0, size + 1);
    size_t bytesRead = fread(content, 1, size, file);
    content[bytesRead] = '\0';
    fclose(file);
    
    // Set correct length (supports binary data containing \\0)
    MoonStrHeader* header = moon_str_get_header(content);
    if (header) {
        header->length = bytesRead;
    }
    
    return moon_string_owned(content);
}

MoonValue* moon_write_file(MoonValue* path, MoonValue* content) {
    if (!moon_is_string(path)) return moon_bool(false);
    
    FILE* file = fopen(path->data.strVal, "wb");
    if (!file) return moon_bool(false);
    
    // For string type use its actual length (supports binary data)
    size_t len = 0;
    const char* str = NULL;
    char* toFree = NULL;
    
    if (content && content->type == MOON_STRING && content->data.strVal) {
        str = content->data.strVal;
        MoonStrHeader* header = moon_str_get_header(content->data.strVal);
        if (header) {
            len = header->length;
        } else {
            len = strlen(str);
        }
    } else {
        toFree = moon_to_string(content);
        str = toFree;
        len = strlen(str);
    }
    
    size_t written = fwrite(str, 1, len, file);
    
    if (toFree) {
        free(toFree);
    }
    fclose(file);
    
    return moon_bool(written == len);
}

MoonValue* moon_append_file(MoonValue* path, MoonValue* content) {
    if (!moon_is_string(path)) return moon_bool(false);
    
    FILE* file = fopen(path->data.strVal, "ab");
    if (!file) return moon_bool(false);
    
    // For string type use its actual length (supports binary data)
    size_t len = 0;
    const char* str = NULL;
    char* toFree = NULL;
    
    if (content && content->type == MOON_STRING && content->data.strVal) {
        str = content->data.strVal;
        MoonStrHeader* header = moon_str_get_header(content->data.strVal);
        if (header) {
            len = header->length;
        } else {
            len = strlen(str);
        }
    } else {
        toFree = moon_to_string(content);
        str = toFree;
        len = strlen(str);
    }
    
    size_t written = fwrite(str, 1, len, file);
    
    if (toFree) {
        free(toFree);
    }
    fclose(file);
    
    return moon_bool(written == len);
}

// Helper: Convert UTF-8 to wide string (Windows only)
#ifdef _WIN32
static wchar_t* utf8_to_wide(const char* utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t* wide = (wchar_t*)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}
#endif

MoonValue* moon_exists(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    DWORD attrs = GetFileAttributesW(widePath);
    free(widePath);
    return moon_bool(attrs != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return moon_bool(stat(path->data.strVal, &st) == 0);
#endif
}

MoonValue* moon_is_file(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    DWORD attrs = GetFileAttributesW(widePath);
    free(widePath);
    return moon_bool(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path->data.strVal, &st) != 0) return moon_bool(false);
    return moon_bool(S_ISREG(st.st_mode));
#endif
}

MoonValue* moon_is_dir(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    DWORD attrs = GetFileAttributesW(widePath);
    free(widePath);
    return moon_bool(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path->data.strVal, &st) != 0) return moon_bool(false);
    return moon_bool(S_ISDIR(st.st_mode));
#endif
}

MoonValue* moon_list_dir(MoonValue* path) {
    MoonValue* result = moon_list_new();
    if (!moon_is_string(path)) return result;
    
#ifdef _WIN32
    // Convert UTF-8 path to wide string
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, path->data.strVal, -1, NULL, 0);
    wchar_t* widePath = (wchar_t*)malloc(pathLen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, path->data.strVal, -1, widePath, pathLen);
    
    // Build search path with wildcard
    wchar_t searchPath[MAX_PATH];
    swprintf(searchPath, MAX_PATH, L"%s\\*", widePath);
    free(widePath);
    
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            // Convert wide string filename back to UTF-8
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, NULL, 0, NULL, NULL);
            char* utf8Name = (char*)malloc(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, utf8Name, utf8Len, NULL, NULL);
            moon_list_append(result, moon_string(utf8Name));
            free(utf8Name);
        }
    } while (FindNextFileW(hFind, &fd));
    
    FindClose(hFind);
#else
    DIR* dir = opendir(path->data.strVal);
    if (!dir) return result;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            moon_list_append(result, moon_string(entry->d_name));
        }
    }
    closedir(dir);
#endif
    
    return result;
}

MoonValue* moon_create_dir(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    BOOL result = CreateDirectoryW(widePath, NULL);
    free(widePath);
    return moon_bool(result != 0);
#else
    return moon_bool(mkdir(path->data.strVal, 0755) == 0);
#endif
}

MoonValue* moon_file_size(MoonValue* path) {
    if (!moon_is_string(path)) return moon_int(0);
    
    FILE* file = fopen(path->data.strVal, "rb");
    if (!file) return moon_int(0);
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return moon_int(size);
}

MoonValue* moon_getcwd(void) {
    char buffer[4096];
    if (getcwd(buffer, sizeof(buffer))) {
        return moon_string(buffer);
    }
    return moon_string("");
}

MoonValue* moon_cd(MoonValue* path) {
    if (moon_is_string(path)) {
        if (chdir(path->data.strVal) == 0) {
            return moon_bool(true);
        }
    }
    return moon_bool(false);
}

// ============================================================================
// File Path Functions
// ============================================================================

MoonValue* moon_join_path(MoonValue* a, MoonValue* b) {
    if (!moon_is_string(a) || !moon_is_string(b)) return moon_string("");
    
    const char* s1 = a->data.strVal;
    const char* s2 = b->data.strVal;
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    
    char* result = (char*)moon_alloc(len1 + len2 + 2);
    strcpy(result, s1);
    
    if (len1 > 0 && s1[len1-1] != '/' && s1[len1-1] != '\\') {
#ifdef _WIN32
        strcat(result, "\\");
#else
        strcat(result, "/");
#endif
    }
    strcat(result, s2);
    return moon_string_owned(result);
}

MoonValue* moon_basename(MoonValue* path) {
    if (!moon_is_string(path)) return moon_string("");
    const char* s = path->data.strVal;
    const char* lastSlash = strrchr(s, '/');
    const char* lastBackslash = strrchr(s, '\\');
    const char* last = lastSlash > lastBackslash ? lastSlash : lastBackslash;
    if (last) return moon_string(last + 1);
    return moon_string(s);
}

MoonValue* moon_dirname(MoonValue* path) {
    if (!moon_is_string(path)) return moon_string("");
    const char* s = path->data.strVal;
    const char* lastSlash = strrchr(s, '/');
    const char* lastBackslash = strrchr(s, '\\');
    const char* last = lastSlash > lastBackslash ? lastSlash : lastBackslash;
    if (!last) return moon_string(".");
    
    size_t len = last - s;
    char* result = (char*)moon_alloc(len + 1);
    memcpy(result, s, len);
    result[len] = '\0';
    return moon_string_owned(result);
}

MoonValue* moon_extension(MoonValue* path) {
    if (!moon_is_string(path)) return moon_string("");
    const char* s = path->data.strVal;
    const char* dot = strrchr(s, '.');
    if (dot && dot != s) return moon_string(dot + 1);
    return moon_string("");
}

MoonValue* moon_absolute_path(MoonValue* path) {
    if (!moon_is_string(path)) return moon_string("");
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    wchar_t wideBuffer[MAX_PATH];
    if (GetFullPathNameW(widePath, MAX_PATH, wideBuffer, NULL)) {
        free(widePath);
        // Convert back to UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideBuffer, -1, NULL, 0, NULL, NULL);
        char* utf8Buffer = (char*)malloc(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0, wideBuffer, -1, utf8Buffer, utf8Len, NULL, NULL);
        MoonValue* result = moon_string(utf8Buffer);
        free(utf8Buffer);
        return result;
    }
    free(widePath);
#else
    char buffer[PATH_MAX];
    if (realpath(path->data.strVal, buffer)) {
        return moon_string(buffer);
    }
#endif
    return moon_string(path->data.strVal);
}

MoonValue* moon_copy_file(MoonValue* src, MoonValue* dst) {
    if (!moon_is_string(src) || !moon_is_string(dst)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* wideSrc = utf8_to_wide(src->data.strVal);
    wchar_t* wideDst = utf8_to_wide(dst->data.strVal);
    BOOL result = CopyFileW(wideSrc, wideDst, FALSE);
    free(wideSrc);
    free(wideDst);
    return moon_bool(result != 0);
#else
    FILE* fsrc = fopen(src->data.strVal, "rb");
    if (!fsrc) return moon_bool(false);
    FILE* fdst = fopen(dst->data.strVal, "wb");
    if (!fdst) { fclose(fsrc); return moon_bool(false); }
    
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fsrc)) > 0) {
        fwrite(buffer, 1, n, fdst);
    }
    fclose(fsrc);
    fclose(fdst);
    return moon_bool(true);
#endif
}

MoonValue* moon_move_file(MoonValue* src, MoonValue* dst) {
    if (!moon_is_string(src) || !moon_is_string(dst)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* wideSrc = utf8_to_wide(src->data.strVal);
    wchar_t* wideDst = utf8_to_wide(dst->data.strVal);
    BOOL result = MoveFileW(wideSrc, wideDst);
    free(wideSrc);
    free(wideDst);
    return moon_bool(result != 0);
#else
    return moon_bool(rename(src->data.strVal, dst->data.strVal) == 0);
#endif
}

MoonValue* moon_remove_file(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    BOOL result = DeleteFileW(widePath);
    free(widePath);
    return moon_bool(result != 0);
#else
    return moon_bool(remove(path->data.strVal) == 0);
#endif
}

MoonValue* moon_remove_dir(MoonValue* path) {
    if (!moon_is_string(path)) return moon_bool(false);
#ifdef _WIN32
    wchar_t* widePath = utf8_to_wide(path->data.strVal);
    BOOL result = RemoveDirectoryW(widePath);
    free(widePath);
    return moon_bool(result != 0);
#else
    return moon_bool(rmdir(path->data.strVal) == 0);
#endif
}

// ============================================================================
// Date/Time Functions
// ============================================================================

MoonValue* moon_now(void) {
    // Return current time in milliseconds
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals since 1601 to milliseconds since 1970
    int64_t ms = (uli.QuadPart - 116444736000000000ULL) / 10000;
    return moon_int(ms);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return moon_int(ms);
#endif
}

// Helper: convert timestamp to time_t, auto-detect milliseconds
static time_t timestamp_to_time_t(MoonValue* timestamp) {
    int64_t ts = moon_to_int(timestamp);
    // If ts > 9999999999 (year ~2286 in seconds), assume it's milliseconds
    if (ts > 9999999999LL) {
        ts = ts / 1000;
    }
    return (time_t)ts;
}

// Global timezone offset in seconds (0 = use system local time, INT_MIN = use system)
static int g_timezone_offset_seconds = INT_MIN;
static bool g_timezone_is_utc = false;
static char g_timezone_name[64] = "system";

// Set timezone by name or offset
// Supported formats:
//   - "system" or "local" - use system local time
//   - "utc" or "UTC" - use UTC
//   - "Asia/Shanghai", "America/New_York", etc. - common timezone names
//   - Integer offset in hours (e.g., 8 for UTC+8, -5 for UTC-5)
MoonValue* moon_set_timezone(MoonValue* tz) {
    if (!tz || moon_is_null(tz)) {
        // Reset to system timezone
        g_timezone_offset_seconds = INT_MIN;
        g_timezone_is_utc = false;
        strcpy(g_timezone_name, "system");
        return moon_bool(true);
    }
    
    if (moon_is_int(tz) || moon_is_float(tz)) {
        // Numeric offset in hours
        double hours = moon_is_int(tz) ? (double)tz->data.intVal : tz->data.floatVal;
        g_timezone_offset_seconds = (int)(hours * 3600);
        g_timezone_is_utc = false;
        snprintf(g_timezone_name, sizeof(g_timezone_name), "UTC%+.0f", hours);
        return moon_bool(true);
    }
    
    if (moon_is_string(tz)) {
        const char* tzStr = tz->data.strVal;
        
        // Check for system/local
        if (strcmp(tzStr, "system") == 0 || strcmp(tzStr, "local") == 0) {
            g_timezone_offset_seconds = INT_MIN;
            g_timezone_is_utc = false;
            strcpy(g_timezone_name, "system");
            return moon_bool(true);
        }
        
        // Check for UTC
        if (strcmp(tzStr, "utc") == 0 || strcmp(tzStr, "UTC") == 0) {
            g_timezone_offset_seconds = 0;
            g_timezone_is_utc = true;
            strcpy(g_timezone_name, "UTC");
            return moon_bool(true);
        }
        
        // Common timezone name mappings (hours from UTC)
        struct { const char* name; int offset_hours; } tz_map[] = {
            // Asia
            {"Asia/Shanghai", 8}, {"Asia/Beijing", 8}, {"CST", 8}, {"China", 8},
            {"Asia/Hong_Kong", 8}, {"Asia/Taipei", 8},
            {"Asia/Tokyo", 9}, {"JST", 9}, {"Japan", 9},
            {"Asia/Seoul", 9}, {"KST", 9}, {"Korea", 9},
            {"Asia/Singapore", 8}, {"SGT", 8},
            {"Asia/Dubai", 4},
            {"Asia/Kolkata", 5}, {"IST", 5},  // India Standard Time
            // Europe
            {"Europe/London", 0}, {"GMT", 0},
            {"Europe/Paris", 1}, {"CET", 1},
            {"Europe/Berlin", 1},
            {"Europe/Moscow", 3}, {"MSK", 3},
            // Americas
            {"America/New_York", -5}, {"EST", -5}, {"EDT", -4},
            {"America/Chicago", -6}, {"CST_US", -6},
            {"America/Denver", -7}, {"MST", -7},
            {"America/Los_Angeles", -8}, {"PST", -8}, {"PDT", -7},
            {"America/Sao_Paulo", -3},
            // Pacific
            {"Australia/Sydney", 10}, {"AEST", 10},
            {"Pacific/Auckland", 12}, {"NZST", 12},
            {NULL, 0}
        };
        
        for (int i = 0; tz_map[i].name != NULL; i++) {
            if (strcmp(tzStr, tz_map[i].name) == 0) {
                g_timezone_offset_seconds = tz_map[i].offset_hours * 3600;
                g_timezone_is_utc = false;
                strncpy(g_timezone_name, tzStr, sizeof(g_timezone_name) - 1);
                return moon_bool(true);
            }
        }
        
        // Try to parse as UTC+X or UTC-X format
        if (strncmp(tzStr, "UTC", 3) == 0) {
            int offset = 0;
            if (sscanf(tzStr + 3, "%d", &offset) == 1) {
                g_timezone_offset_seconds = offset * 3600;
                g_timezone_is_utc = false;
                strncpy(g_timezone_name, tzStr, sizeof(g_timezone_name) - 1);
                return moon_bool(true);
            }
        }
        
        // Unknown timezone name - return false
        return moon_bool(false);
    }
    
    return moon_bool(false);
}

// Get current timezone name
MoonValue* moon_get_timezone(void) {
    return moon_string(g_timezone_name);
}

// Helper: check if timezone is UTC
static bool is_utc_timezone(MoonValue* tz) {
    if (!tz || moon_is_null(tz)) {
        // Use global setting
        return g_timezone_is_utc;
    }
    if (!moon_is_string(tz)) return false;
    const char* tzStr = tz->data.strVal;
    return (strcmp(tzStr, "utc") == 0 || strcmp(tzStr, "UTC") == 0);
}

// Helper: get tm struct based on timezone
static struct tm* get_tm_info(time_t t, MoonValue* tz) {
    // If explicit timezone parameter is provided, use it
    if (tz && !moon_is_null(tz) && moon_is_string(tz)) {
        if (is_utc_timezone(tz)) {
            return gmtime(&t);
        }
        return localtime(&t);
    }
    
    // Use global timezone setting
    if (g_timezone_offset_seconds == INT_MIN) {
        // System local time
        return localtime(&t);
    }
    
    if (g_timezone_is_utc || g_timezone_offset_seconds == 0) {
        return gmtime(&t);
    }
    
    // Apply custom offset: convert to UTC, then add offset
    static struct tm result;
    time_t adjusted_t = t + g_timezone_offset_seconds;
    
    // Get UTC time for adjusted timestamp
    struct tm* utc_tm = gmtime(&adjusted_t);
    if (utc_tm) {
        memcpy(&result, utc_tm, sizeof(struct tm));
        return &result;
    }
    
    return localtime(&t);
}

MoonValue* moon_date_format(MoonValue* timestamp, MoonValue* fmt, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_string("");
    
    const char* format = "%Y-%m-%d %H:%M:%S";
    if (moon_is_string(fmt)) {
        format = fmt->data.strVal;
    }
    
    char buffer[256];
    strftime(buffer, sizeof(buffer), format, tm_info);
    return moon_string(buffer);
}

MoonValue* moon_year(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_year + 1900);
}

MoonValue* moon_month(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_mon + 1);
}

MoonValue* moon_day(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_mday);
}

MoonValue* moon_hour(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_hour);
}

MoonValue* moon_minute(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_min);
}

MoonValue* moon_second(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_sec);
}

MoonValue* moon_weekday(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_wday);
}

MoonValue* moon_make_time(MoonValue* y, MoonValue* m, MoonValue* d, MoonValue* h, MoonValue* mi, MoonValue* s) {
    struct tm tm_info = {0};
    tm_info.tm_year = (int)moon_to_int(y) - 1900;
    tm_info.tm_mon = (int)moon_to_int(m) - 1;
    tm_info.tm_mday = (int)moon_to_int(d);
    tm_info.tm_hour = (int)moon_to_int(h);
    tm_info.tm_min = (int)moon_to_int(mi);
    tm_info.tm_sec = (int)moon_to_int(s);
    time_t t = mktime(&tm_info);
    return moon_int((int64_t)t);
}

MoonValue* moon_days_in_month(MoonValue* year, MoonValue* month) {
    int y = (int)moon_to_int(year);
    int m = (int)moon_to_int(month);
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return moon_int(0);
    int d = days[m - 1];
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) d = 29;
    return moon_int(d);
}

MoonValue* moon_is_leap_year(MoonValue* year) {
    int y = (int)moon_to_int(year);
    return moon_bool(y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

// ============================================================================
// Extended Date/Time Functions
// ============================================================================

// Get Unix timestamp in seconds
MoonValue* moon_unix_time(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    int64_t secs = (uli.QuadPart - 116444736000000000ULL) / 10000000;
    return moon_int(secs);
#else
    return moon_int((int64_t)time(NULL));
#endif
}

// Parse date string to timestamp
MoonValue* moon_date_parse(MoonValue* str, MoonValue* fmt) {
    if (!moon_is_string(str)) return moon_int(0);
    const char* dateStr = str->data.strVal;
    const char* format = "%Y-%m-%d %H:%M:%S";
    if (moon_is_string(fmt)) {
        format = fmt->data.strVal;
    }
    
    struct tm tm_info = {0};
#ifdef _WIN32
    // Windows doesn't have strptime, use sscanf for common formats
    int y = 0, m = 0, d = 0, h = 0, mi = 0, s = 0;
    if (strcmp(format, "%Y-%m-%d %H:%M:%S") == 0) {
        sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &mi, &s);
    } else if (strcmp(format, "%Y-%m-%d") == 0) {
        sscanf(dateStr, "%d-%d-%d", &y, &m, &d);
    } else if (strcmp(format, "%Y/%m/%d %H:%M:%S") == 0) {
        sscanf(dateStr, "%d/%d/%d %d:%d:%d", &y, &m, &d, &h, &mi, &s);
    } else if (strcmp(format, "%Y/%m/%d") == 0) {
        sscanf(dateStr, "%d/%d/%d", &y, &m, &d);
    } else if (strcmp(format, "%d-%m-%Y") == 0) {
        sscanf(dateStr, "%d-%d-%d", &d, &m, &y);
    } else if (strcmp(format, "%d/%m/%Y") == 0) {
        sscanf(dateStr, "%d/%d/%d", &d, &m, &y);
    } else {
        // Default: try Y-m-d H:M:S
        sscanf(dateStr, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &mi, &s);
    }
    tm_info.tm_year = y - 1900;
    tm_info.tm_mon = m - 1;
    tm_info.tm_mday = d;
    tm_info.tm_hour = h;
    tm_info.tm_min = mi;
    tm_info.tm_sec = s;
#else
    strptime(dateStr, format, &tm_info);
#endif
    
    time_t t = mktime(&tm_info);
    // Return milliseconds for consistency with now()
    return moon_int((int64_t)t * 1000);
}

// Get millisecond part (0-999)
MoonValue* moon_millisecond(MoonValue* timestamp) {
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        // It's in milliseconds, get the remainder
        return moon_int(ts % 1000);
    }
    // It's in seconds, no millisecond info
    return moon_int(0);
}

// Get day of year (1-366)
MoonValue* moon_day_of_year(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int(tm_info->tm_yday + 1);
}

// Get week of year (1-53, ISO week)
MoonValue* moon_week_of_year(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    
    // Calculate ISO week number
    int yday = tm_info->tm_yday;
    int wday = tm_info->tm_wday;
    // Adjust Sunday (0) to be 7
    if (wday == 0) wday = 7;
    
    // Find the first Thursday of the year
    int week = (yday - wday + 10) / 7;
    if (week < 1) week = 52;
    if (week > 52) week = 1;
    
    return moon_int(week);
}

// Get quarter (1-4)
MoonValue* moon_quarter(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_int(0);
    return moon_int((tm_info->tm_mon / 3) + 1);
}

// Get timezone name/offset
MoonValue* moon_timezone(void) {
#ifdef _WIN32
    TIME_ZONE_INFORMATION tzInfo;
    GetTimeZoneInformation(&tzInfo);
    // Convert wide string to narrow string
    char tzName[128];
    WideCharToMultiByte(CP_UTF8, 0, tzInfo.StandardName, -1, tzName, sizeof(tzName), NULL, NULL);
    return moon_string(tzName);
#else
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    return moon_string(tm_info->tm_zone ? tm_info->tm_zone : "UTC");
#endif
}

// Get UTC offset in seconds
MoonValue* moon_utc_offset(void) {
#ifdef _WIN32
    TIME_ZONE_INFORMATION tzInfo;
    GetTimeZoneInformation(&tzInfo);
    // Bias is in minutes, negative for east of UTC
    return moon_int(-tzInfo.Bias * 60);
#else
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    return moon_int(tm_info->tm_gmtoff);
#endif
}

// Add seconds to timestamp
MoonValue* moon_add_seconds(MoonValue* timestamp, MoonValue* seconds) {
    int64_t ts = moon_to_int(timestamp);
    int64_t secs = moon_to_int(seconds);
    
    if (ts > 9999999999LL) {
        // Milliseconds
        return moon_int(ts + secs * 1000);
    }
    return moon_int(ts + secs);
}

// Add minutes to timestamp
MoonValue* moon_add_minutes(MoonValue* timestamp, MoonValue* minutes) {
    int64_t ts = moon_to_int(timestamp);
    int64_t mins = moon_to_int(minutes);
    
    if (ts > 9999999999LL) {
        return moon_int(ts + mins * 60 * 1000);
    }
    return moon_int(ts + mins * 60);
}

// Add hours to timestamp
MoonValue* moon_add_hours(MoonValue* timestamp, MoonValue* hours) {
    int64_t ts = moon_to_int(timestamp);
    int64_t hrs = moon_to_int(hours);
    
    if (ts > 9999999999LL) {
        return moon_int(ts + hrs * 3600 * 1000);
    }
    return moon_int(ts + hrs * 3600);
}

// Add days to timestamp
MoonValue* moon_add_days(MoonValue* timestamp, MoonValue* days) {
    int64_t ts = moon_to_int(timestamp);
    int64_t d = moon_to_int(days);
    
    if (ts > 9999999999LL) {
        return moon_int(ts + d * 86400 * 1000);
    }
    return moon_int(ts + d * 86400);
}

// Add months to timestamp
MoonValue* moon_add_months(MoonValue* timestamp, MoonValue* months) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    int m = moon_to_int(months);
    new_tm.tm_mon += m;
    
    // Normalize
    while (new_tm.tm_mon > 11) {
        new_tm.tm_mon -= 12;
        new_tm.tm_year++;
    }
    while (new_tm.tm_mon < 0) {
        new_tm.tm_mon += 12;
        new_tm.tm_year--;
    }
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Add years to timestamp
MoonValue* moon_add_years(MoonValue* timestamp, MoonValue* years) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    new_tm.tm_year += moon_to_int(years);
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Difference in seconds between two timestamps
MoonValue* moon_diff_seconds(MoonValue* ts1, MoonValue* ts2) {
    int64_t t1 = moon_to_int(ts1);
    int64_t t2 = moon_to_int(ts2);
    
    // Normalize to seconds
    if (t1 > 9999999999LL) t1 /= 1000;
    if (t2 > 9999999999LL) t2 /= 1000;
    
    return moon_int(t2 - t1);
}

// Difference in days between two timestamps
MoonValue* moon_diff_days(MoonValue* ts1, MoonValue* ts2) {
    int64_t t1 = moon_to_int(ts1);
    int64_t t2 = moon_to_int(ts2);
    
    // Normalize to seconds
    if (t1 > 9999999999LL) t1 /= 1000;
    if (t2 > 9999999999LL) t2 /= 1000;
    
    return moon_int((t2 - t1) / 86400);
}

// Get start of day (00:00:00)
MoonValue* moon_start_of_day(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    new_tm.tm_hour = 0;
    new_tm.tm_min = 0;
    new_tm.tm_sec = 0;
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Get end of day (23:59:59)
MoonValue* moon_end_of_day(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    new_tm.tm_hour = 23;
    new_tm.tm_min = 59;
    new_tm.tm_sec = 59;
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Get start of month (1st day, 00:00:00)
MoonValue* moon_start_of_month(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    new_tm.tm_mday = 1;
    new_tm.tm_hour = 0;
    new_tm.tm_min = 0;
    new_tm.tm_sec = 0;
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Get end of month (last day, 23:59:59)
MoonValue* moon_end_of_month(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return timestamp;
    
    struct tm new_tm = *tm_info;
    // Get days in current month
    int y = new_tm.tm_year + 1900;
    int m = new_tm.tm_mon + 1;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int lastDay = days[m - 1];
    if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) lastDay = 29;
    
    new_tm.tm_mday = lastDay;
    new_tm.tm_hour = 23;
    new_tm.tm_min = 59;
    new_tm.tm_sec = 59;
    
    time_t newT = mktime(&new_tm);
    int64_t ts = moon_to_int(timestamp);
    if (ts > 9999999999LL) {
        return moon_int((int64_t)newT * 1000);
    }
    return moon_int((int64_t)newT);
}

// Check if weekend (Saturday or Sunday)
MoonValue* moon_is_weekend(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    struct tm* tm_info = get_tm_info(t, tz);
    if (!tm_info) return moon_bool(false);
    return moon_bool(tm_info->tm_wday == 0 || tm_info->tm_wday == 6);
}

// Check if today
MoonValue* moon_is_today(MoonValue* timestamp, MoonValue* tz) {
    time_t t = timestamp_to_time_t(timestamp);
    time_t now_t = time(NULL);
    
    struct tm* tm1 = get_tm_info(t, tz);
    struct tm* tm2 = get_tm_info(now_t, tz);
    
    if (!tm1 || !tm2) return moon_bool(false);
    
    return moon_bool(tm1->tm_year == tm2->tm_year && 
                     tm1->tm_mon == tm2->tm_mon && 
                     tm1->tm_mday == tm2->tm_mday);
}

// Check if same day
MoonValue* moon_is_same_day(MoonValue* ts1, MoonValue* ts2, MoonValue* tz) {
    time_t t1 = timestamp_to_time_t(ts1);
    time_t t2 = timestamp_to_time_t(ts2);
    
    struct tm* tm1 = get_tm_info(t1, tz);
    struct tm tm1_copy;
    if (tm1) tm1_copy = *tm1;
    else return moon_bool(false);
    
    struct tm* tm2 = get_tm_info(t2, tz);
    if (!tm2) return moon_bool(false);
    
    return moon_bool(tm1_copy.tm_year == tm2->tm_year && 
                     tm1_copy.tm_mon == tm2->tm_mon && 
                     tm1_copy.tm_mday == tm2->tm_mday);
}
