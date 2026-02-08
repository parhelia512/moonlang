// MoonLang Runtime - Built-in Functions Module
// Copyright (c) 2026 greenteng.com
//
// Built-in functions: print, input, type, len, format, etc.

#include "moonrt_core.h"

// ============================================================================
// I/O Functions
// ============================================================================

void moon_print(MoonValue** args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        char* str = moon_to_string(args[i]);
        printf("%s", str);
        free(str);
    }
    printf("\n");
    fflush(stdout);
}

MoonValue* moon_input(MoonValue* prompt) {
    if (prompt && moon_is_string(prompt)) {
        printf("%s", prompt->data.strVal);
        fflush(stdout);
    }
    
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        return moon_string(buffer);
    }
    return moon_string("");
}

// ============================================================================
// Type Functions
// ============================================================================

MoonValue* moon_type(MoonValue* val) {
    if (!val) return moon_string("null");
    
    switch (val->type) {
        case MOON_NULL: return moon_string("null");
        case MOON_INT: return moon_string("int");
        case MOON_FLOAT: return moon_string("float");
        case MOON_BOOL: return moon_string("bool");
        case MOON_STRING: return moon_string("string");
        case MOON_LIST: return moon_string("list");
        case MOON_DICT: return moon_string("dict");
        case MOON_FUNC: return moon_string("function");
        case MOON_OBJECT: return moon_string("object");
        case MOON_CLASS: return moon_string("class");
        default: return moon_string("unknown");
    }
}

MoonValue* moon_len(MoonValue* val) {
    if (!val) return moon_int(0);
    
    switch (val->type) {
        case MOON_STRING: {
            // Use MoonStrHeader for actual length (supports binary data and \\0)
            MoonStrHeader* header = moon_str_get_header(val->data.strVal);
            if (header) {
                return moon_int(header->length);
            }
            // Fallback to strlen (for non-owned strings)
            return moon_int(strlen(val->data.strVal));
        }
        case MOON_LIST: return moon_int(val->data.listVal->length);
        case MOON_DICT: return moon_int(val->data.dictVal->length);
        default: return moon_int(0);
    }
}

// ============================================================================
// Formatting
// ============================================================================

MoonValue* moon_format(MoonValue** args, int argc) {
    if (argc == 0 || !moon_is_string(args[0])) {
        return moon_string("");
    }
    
    const char* fmt = args[0]->data.strVal;
    size_t bufSize = strlen(fmt) * 2 + 256;
    char* result = (char*)moon_alloc(bufSize);
    char* dest = result;
    
    int argIdx = 1;
    while (*fmt) {
        if (*fmt == '{' && fmt[1] == '}') {
            if (argIdx < argc) {
                char* argStr = moon_to_string(args[argIdx++]);
                size_t argLen = strlen(argStr);
                size_t usedLen = dest - result;
                if (usedLen + argLen + 1 >= bufSize) {
                    bufSize = (usedLen + argLen + 256) * 2;
                    result = (char*)realloc(result, bufSize);
                    dest = result + usedLen;
                }
                memcpy(dest, argStr, argLen);
                dest += argLen;
                free(argStr);
            }
            fmt += 2;
        } else {
            *dest++ = *fmt++;
        }
    }
    *dest = '\0';
    
    return moon_string_owned(result);
}

// ============================================================================
// System Functions
// ============================================================================

MoonValue* moon_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return moon_float((double)counter.QuadPart * 1000.0 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return moon_float(ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0);
#endif
}

void moon_sleep(MoonValue* ms) {
    int64_t millis = moon_to_int(ms);
    if (millis <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)millis);
#else
    usleep(millis * 1000);
#endif
}

MoonValue* moon_shell(MoonValue* cmd) {
    if (!moon_is_string(cmd)) return moon_int(-1);
    int result = system(cmd->data.strVal);
    return moon_int(result);
}

MoonValue* moon_shell_output(MoonValue* cmd) {
    if (!moon_is_string(cmd)) return moon_string("");
    
#ifdef _WIN32
    FILE* pipe = _popen(cmd->data.strVal, "r");
#else
    FILE* pipe = popen(cmd->data.strVal, "r");
#endif
    if (!pipe) return moon_string("");
    
    char buffer[4096];
    size_t totalSize = 0;
    size_t bufferSize = 4096;
    char* result = (char*)moon_alloc(bufferSize);
    result[0] = '\0';
    
    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        if (totalSize + len >= bufferSize) {
            bufferSize *= 2;
            result = (char*)realloc(result, bufferSize);
        }
        memcpy(result + totalSize, buffer, len + 1);
        totalSize += len;
    }
    
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    
    return moon_string_owned(result);
}

MoonValue* moon_env(MoonValue* name) {
    if (!moon_is_string(name)) return moon_null();
    const char* val = getenv(name->data.strVal);
    return val ? moon_string(val) : moon_null();
}

void moon_set_env(MoonValue* name, MoonValue* value) {
    if (!moon_is_string(name)) return;
    char* valStr = moon_to_string(value);
#ifdef _WIN32
    SetEnvironmentVariableA(name->data.strVal, valStr);
#else
    setenv(name->data.strVal, valStr, 1);
#endif
    free(valStr);
}

void moon_exit(MoonValue* code) {
    exit((int)moon_to_int(code));
}

MoonValue* moon_argv(void) {
    MoonValue* result = moon_list_new();
    for (int i = 0; i < g_argc; i++) {
        moon_list_append(result, moon_string(g_argv[i]));
    }
    return result;
}

// ============================================================================
// String Encryption/Decryption
// ============================================================================

MoonValue* moon_decrypt_string(MoonValue* encrypted) {
    if (!moon_is_string(encrypted)) return moon_string("");
    
    const char* data = encrypted->data.strVal;
    size_t len = strlen(data);
    
    if (len < 8) return moon_string("");
    
    unsigned char key[4];
    for (int i = 0; i < 4; i++) {
        char hex[3] = {data[i*2], data[i*2+1], 0};
        key[i] = (unsigned char)strtol(hex, NULL, 16);
    }
    
    size_t encLen = (len - 8) / 2;
    char* result = (char*)malloc(encLen + 1);
    
    for (size_t i = 0; i < encLen; i++) {
        char hex[3] = {data[8 + i*2], data[8 + i*2 + 1], 0};
        unsigned char c = (unsigned char)strtol(hex, NULL, 16);
        result[i] = c ^ key[i % 4];
    }
    result[encLen] = '\0';
    
    MoonValue* val = moon_string(result);
    free(result);
    return val;
}

// ============================================================================
// Extended System Functions
// ============================================================================

MoonValue* moon_platform(void) {
#ifdef _WIN32
    return moon_string("windows");
#elif __APPLE__
    return moon_string("macos");
#elif __linux__
    return moon_string("linux");
#else
    return moon_string("unknown");
#endif
}

MoonValue* moon_getpid(void) {
#ifdef _WIN32
    return moon_int(GetCurrentProcessId());
#else
    return moon_int(getpid());
#endif
}

MoonValue* moon_system(MoonValue* cmd) {
    if (!moon_is_string(cmd)) return moon_int(-1);
    return moon_int(system(cmd->data.strVal));
}

MoonValue* moon_exec(MoonValue* cmd) {
    return moon_shell_output(cmd);
}

// ============================================================================
// Memory Management Functions
// ============================================================================

MoonValue* moon_mem_stats(void) {
    size_t used = 0, peak = 0, total = 0;
    moon_heap_stats(&used, &peak, &total);
    
    MoonValue* result = moon_dict_new();
    moon_dict_set(result, moon_string("used"), moon_int((int64_t)used));
    moon_dict_set(result, moon_string("peak"), moon_int((int64_t)peak));
    moon_dict_set(result, moon_string("total"), moon_int((int64_t)total));
    moon_dict_set(result, moon_string("free"), moon_int((int64_t)(total - used)));
    
#ifdef MOON_USE_STATIC_HEAP
    moon_dict_set(result, moon_string("type"), moon_string("static"));
#else
    moon_dict_set(result, moon_string("type"), moon_string("dynamic"));
#endif
    
    return result;
}

void moon_mem_reset(void) {
    moon_heap_reset();
}

MoonValue* moon_target_info(void) {
    MoonValue* result = moon_dict_new();
    
#ifdef MOON_TARGET_MCU
    moon_dict_set(result, moon_string("target"), moon_string("mcu"));
#elif defined(MOON_TARGET_EMBEDDED)
    moon_dict_set(result, moon_string("target"), moon_string("embedded"));
#else
    moon_dict_set(result, moon_string("target"), moon_string("native"));
#endif
    
#ifdef MOON_HAS_GUI
    moon_dict_set(result, moon_string("gui"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("gui"), moon_bool(false));
#endif

#ifdef MOON_HAS_NETWORK
    moon_dict_set(result, moon_string("network"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("network"), moon_bool(false));
#endif

#ifdef MOON_HAS_DLL
    moon_dict_set(result, moon_string("dll"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("dll"), moon_bool(false));
#endif

#ifdef MOON_HAS_REGEX
    moon_dict_set(result, moon_string("regex"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("regex"), moon_bool(false));
#endif

#ifdef MOON_HAS_JSON
    moon_dict_set(result, moon_string("json"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("json"), moon_bool(false));
#endif

#ifdef MOON_HAS_FLOAT
    moon_dict_set(result, moon_string("float"), moon_bool(true));
#else
    moon_dict_set(result, moon_string("float"), moon_bool(false));
#endif

#ifdef MOON_USE_STATIC_HEAP
    moon_dict_set(result, moon_string("static_heap"), moon_bool(true));
    moon_dict_set(result, moon_string("heap_size"), moon_int(MOON_HEAP_SIZE));
#else
    moon_dict_set(result, moon_string("static_heap"), moon_bool(false));
    moon_dict_set(result, moon_string("heap_size"), moon_int(0));
#endif

    return result;
}
