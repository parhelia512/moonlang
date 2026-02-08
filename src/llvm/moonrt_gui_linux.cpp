// MoonLang Runtime - GUI Support for Linux (GTK3 + WebKitGTK)
// Copyright (c) 2026 greenteng.com
//
// Linux GUI implementation using GTK3 and WebKitGTK
// Provides the same API as the Windows WebView2 version

#include "moonrt.h"
#include "moonrt_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// If MOON_NO_GUI is defined, skip to stub implementations
#if defined(MOON_PLATFORM_LINUX) && !defined(MOON_NO_GUI)

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gdk/gdk.h>
#include <map>
#include <string>
#include <unordered_map>

// Detect WebKit version for API compatibility
// webkit2gtk-4.1 deprecates webkit_web_view_run_javascript in favor of webkit_web_view_evaluate_javascript
#if WEBKIT_MAJOR_VERSION > 2 || (WEBKIT_MAJOR_VERSION == 2 && WEBKIT_MINOR_VERSION >= 40)
#define USE_WEBKIT_EVALUATE_JAVASCRIPT 1
#else
#define USE_WEBKIT_EVALUATE_JAVASCRIPT 0
#endif

// Helper function to run JavaScript with version compatibility
static void moon_webkit_run_javascript(WebKitWebView* webview, const char* script,
                                       GAsyncReadyCallback callback, gpointer data) {
#if USE_WEBKIT_EVALUATE_JAVASCRIPT
    webkit_web_view_evaluate_javascript(webview, script, -1, NULL, NULL, NULL, callback, data);
#else
    webkit_web_view_run_javascript(webview, script, NULL, callback, data);
#endif
}

// For system tray (AppIndicator)
#ifdef HAVE_APPINDICATOR
#include <libappindicator/app-indicator.h>
#endif

// ============================================================================
// MoonWindow Structure (Multi-Window Support)
// ============================================================================

struct MoonWindow {
    int id;                              // Unique window ID
    GtkWidget* window;                   // GTK window
    GtkWidget* webview;                  // WebKitWebView
    bool webviewReady;
    bool showPending;
    
    // Options
    bool frameless;
    bool transparent;
    bool topmost;
    bool resizable;
    bool clickThrough;
    bool devtools;
    int alpha;
    
    // Callbacks
    MoonValue* messageCallback;
    MoonValue* closeCallback;
    
    // Exposed functions (callable from JS)
    std::unordered_map<std::string, MoonValue*> exposedFuncs;
    
    // Pending content to load
    char* pendingHtml;
    char* pendingUrl;
    
    // Parent window
    int parentId;
    bool modal;
    
    MoonWindow() : id(0), window(NULL), webview(NULL), webviewReady(false), 
        showPending(false), frameless(false), transparent(false), topmost(false),
        resizable(true), clickThrough(false), devtools(false), alpha(255),
        messageCallback(nullptr), closeCallback(nullptr),
        pendingHtml(nullptr), pendingUrl(nullptr),
        parentId(0), modal(false) {}
    
    ~MoonWindow() {
        if (pendingHtml) free(pendingHtml);
        if (pendingUrl) free(pendingUrl);
        if (messageCallback) moon_release(messageCallback);
        if (closeCallback) moon_release(closeCallback);
        for (auto& pair : exposedFuncs) {
            if (pair.second) moon_release(pair.second);
        }
    }
};

// ============================================================================
// Global State
// ============================================================================

static bool g_gui_initialized = false;
static std::map<int, MoonWindow*> g_windows;
static int g_nextWindowId = 1;
static bool g_running = false;

// System tray (AppIndicator)
#ifdef HAVE_APPINDICATOR
static AppIndicator* g_indicator = NULL;
static GtkWidget* g_trayMenu = NULL;
#endif
static MoonValue* g_trayCallback = NULL;

// Virtual file system for moon:// protocol
static std::unordered_map<std::string, std::string> g_virtualFiles;

// ============================================================================
// Helper Functions
// ============================================================================

static MoonWindow* GetWindowById(int id) {
    auto it = g_windows.find(id);
    return (it != g_windows.end()) ? it->second : nullptr;
}

static MoonWindow* GetFirstWindow() {
    return g_windows.empty() ? nullptr : g_windows.begin()->second;
}

// Get MIME type from file extension
static const char* GetMimeType(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";
    
    std::string ext = path.substr(dotPos);
    // Convert to lowercase
    for (auto& c : ext) c = tolower(c);
    
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".webp") return "image/webp";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".xml") return "application/xml";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

// Register virtual file for moon:// protocol
static void RegisterVirtualFile(const char* path, const char* content, size_t len) {
    std::string spath(path);
    // Normalize path: lowercase, forward slashes
    for (auto& c : spath) {
        if (c == '\\') c = '/';
        c = tolower(c);
    }
    g_virtualFiles[spath] = std::string(content, len);
}

// ============================================================================
// WebView JavaScript Bridge
// ============================================================================

// Inject MoonGUI JavaScript API
static const char* MOONGUI_JS = R"JS(
window.MoonGUI = {
    postMessage: function(msg) {
        window.webkit.messageHandlers.moonMessage.postMessage(JSON.stringify(msg));
    },
    call: function(funcName, ...args) {
        return new Promise((resolve, reject) => {
            const callId = Date.now() + '_' + Math.random();
            window._moonCallbacks = window._moonCallbacks || {};
            window._moonCallbacks[callId] = { resolve, reject };
            window.webkit.messageHandlers.moonCall.postMessage(JSON.stringify({
                callId: callId,
                func: funcName,
                args: args
            }));
        });
    }
};

// Alias for compatibility
window.chrome = window.chrome || {};
window.chrome.webview = window.chrome.webview || {
    postMessage: function(msg) {
        window.MoonGUI.postMessage(msg);
    }
};
)JS";

// ============================================================================
// GTK Signal Handlers
// ============================================================================

static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
    MoonWindow* win = (MoonWindow*)data;
    
    if (win->closeCallback) {
        MoonValue* args[1] = { moon_int(win->id) };
        MoonValue* result = moon_call_func(win->closeCallback, args, 1);
        if (result) {
            bool shouldClose = moon_to_bool(result);
            moon_release(result);
            if (!shouldClose) return TRUE; // Prevent close
        }
    }
    
    // Remove from windows map
    g_windows.erase(win->id);
    delete win;
    
    // Quit if no windows left
    if (g_windows.empty()) {
        gtk_main_quit();
        g_running = false;
    }
    
    return FALSE;
}

static void on_webview_load_changed(WebKitWebView* webview, WebKitLoadEvent event, gpointer data) {
    MoonWindow* win = (MoonWindow*)data;
    
    if (event == WEBKIT_LOAD_FINISHED) {
        win->webviewReady = true;
        
        // Inject MoonGUI JS API
        moon_webkit_run_javascript(webview, MOONGUI_JS, NULL, NULL);
        
        // Show window if pending
        if (win->showPending) {
            gtk_widget_show_all(win->window);
            win->showPending = false;
        }
    }
}

// Handle messages from JavaScript
static void on_script_message(WebKitUserContentManager* manager,
                              WebKitJavascriptResult* result,
                              gpointer data) {
    MoonWindow* win = (MoonWindow*)data;
    
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    if (jsc_value_is_string(value)) {
        char* message = jsc_value_to_string(value);
        
        if (win->messageCallback) {
            MoonValue* args[2] = { moon_int(win->id), moon_string(message) };
            MoonValue* result = moon_call_func(win->messageCallback, args, 2);
            if (result) moon_release(result);
            moon_release(args[0]);
            moon_release(args[1]);
        }
        
        g_free(message);
    }
}

// ============================================================================
// Custom URI Scheme Handler (moon://)
// ============================================================================

static void on_uri_scheme_request(WebKitURISchemeRequest* request, gpointer data) {
    const char* uri = webkit_uri_scheme_request_get_uri(request);
    
    // Parse moon://path
    std::string path;
    if (strncmp(uri, "moon://", 7) == 0) {
        path = uri + 7;
        // Remove "localhost/" prefix if present
        if (path.find("localhost/") == 0) {
            path = path.substr(10);
        }
    }
    
    // Normalize path
    for (auto& c : path) {
        if (c == '\\') c = '/';
        c = tolower(c);
    }
    
    // Look up in virtual file system
    auto it = g_virtualFiles.find(path);
    if (it != g_virtualFiles.end()) {
        const std::string& content = it->second;
        GInputStream* stream = g_memory_input_stream_new_from_data(
            g_memdup2(content.data(), content.size()),
            content.size(),
            g_free
        );
        
        const char* mimeType = GetMimeType(path);
        webkit_uri_scheme_request_finish(request, stream, content.size(), mimeType);
        g_object_unref(stream);
    } else {
        // File not found
        GError* error = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found: %s", path.c_str());
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
    }
}

// ============================================================================
// GUI Initialization
// ============================================================================

MoonValue* moon_gui_init(void) {
    if (g_gui_initialized) return moon_bool(true);
    
    // Initialize GTK
    if (!gtk_init_check(NULL, NULL)) {
        fprintf(stderr, "Failed to initialize GTK\n");
        return moon_bool(false);
    }
    
    // Register moon:// URI scheme
    WebKitWebContext* context = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(context, "moon", on_uri_scheme_request, NULL, NULL);
    
    g_gui_initialized = true;
    return moon_bool(true);
}

// ============================================================================
// Window Creation
// ============================================================================

MoonValue* moon_gui_create(MoonValue* options) {
    if (!g_gui_initialized) {
        moon_gui_init();
    }
    
    MoonWindow* win = new MoonWindow();
    win->id = g_nextWindowId++;
    
    // Parse options
    const char* title = "MoonLang";
    int width = 800;
    int height = 600;
    bool center = true;
    int x = -1, y = -1;
    
    if (options && moon_is_dict(options)) {
        MoonValue* key;
        MoonValue* val;
        
        key = moon_string("title");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_string(val)) title = val->data.strVal;
        moon_release(key);
        
        key = moon_string("width");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_int(val)) width = (int)moon_to_int(val);
        moon_release(key);
        
        key = moon_string("height");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_int(val)) height = (int)moon_to_int(val);
        moon_release(key);
        
        key = moon_string("frameless");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) win->frameless = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("transparent");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) win->transparent = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("topmost");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) win->topmost = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("resizable");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) win->resizable = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("devtools");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) win->devtools = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("center");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_bool(val)) center = moon_to_bool(val);
        moon_release(key);
        
        key = moon_string("x");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_int(val)) { x = (int)moon_to_int(val); center = false; }
        moon_release(key);
        
        key = moon_string("y");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_int(val)) { y = (int)moon_to_int(val); center = false; }
        moon_release(key);
        
        key = moon_string("alpha");
        val = moon_dict_get(options, key, moon_null());
        if (moon_is_int(val)) win->alpha = (int)moon_to_int(val);
        else if (moon_is_float(val)) win->alpha = (int)(moon_to_float(val) * 255);
        moon_release(key);
    }
    
    // Create GTK window
    win->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win->window), title);
    gtk_window_set_default_size(GTK_WINDOW(win->window), width, height);
    
    // Window options
    if (win->frameless) {
        gtk_window_set_decorated(GTK_WINDOW(win->window), FALSE);
    }
    
    if (!win->resizable) {
        gtk_window_set_resizable(GTK_WINDOW(win->window), FALSE);
    }
    
    if (win->topmost) {
        gtk_window_set_keep_above(GTK_WINDOW(win->window), TRUE);
    }
    
    // Transparency
    if (win->transparent || win->alpha < 255) {
        GdkScreen* screen = gtk_widget_get_screen(win->window);
        GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
        if (visual) {
            gtk_widget_set_visual(win->window, visual);
        }
        gtk_widget_set_app_paintable(win->window, TRUE);
        
        if (win->alpha < 255) {
            gtk_widget_set_opacity(win->window, win->alpha / 255.0);
        }
    }
    
    // Position
    if (center) {
        gtk_window_set_position(GTK_WINDOW(win->window), GTK_WIN_POS_CENTER);
    } else if (x >= 0 && y >= 0) {
        gtk_window_move(GTK_WINDOW(win->window), x, y);
    }
    
    // Create WebKitWebView
    WebKitUserContentManager* contentManager = webkit_user_content_manager_new();
    
    // Register script message handlers
    g_signal_connect(contentManager, "script-message-received::moonMessage",
                     G_CALLBACK(on_script_message), win);
    webkit_user_content_manager_register_script_message_handler(contentManager, "moonMessage");
    
    g_signal_connect(contentManager, "script-message-received::moonCall",
                     G_CALLBACK(on_script_message), win);
    webkit_user_content_manager_register_script_message_handler(contentManager, "moonCall");
    
    win->webview = webkit_web_view_new_with_user_content_manager(contentManager);
    
    // WebView settings
    WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(win->webview));
    webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, win->devtools);
    
    // Transparent WebView background
    if (win->transparent) {
        GdkRGBA transparent = {0, 0, 0, 0};
        webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(win->webview), &transparent);
    }
    
    // Add WebView to window
    gtk_container_add(GTK_CONTAINER(win->window), win->webview);
    
    // Connect signals
    g_signal_connect(win->window, "delete-event", G_CALLBACK(on_window_delete), win);
    g_signal_connect(win->webview, "load-changed", G_CALLBACK(on_webview_load_changed), win);
    
    // Store window
    g_windows[win->id] = win;
    
    return moon_int(win->id);
}

// Advanced window creation (compatibility with Windows API)
MoonValue* moon_gui_create_advanced(MoonValue* title, MoonValue* width, MoonValue* height, MoonValue* options) {
    // Build options dict
    MoonValue* opts = moon_dict_new();
    
    if (moon_is_string(title)) {
        MoonValue* key = moon_string("title");
        moon_dict_set(opts, key, title);
        moon_release(key);
    }
    
    if (moon_is_int(width) || moon_is_float(width)) {
        MoonValue* key = moon_string("width");
        moon_dict_set(opts, key, width);
        moon_release(key);
    }
    
    if (moon_is_int(height) || moon_is_float(height)) {
        MoonValue* key = moon_string("height");
        moon_dict_set(opts, key, height);
        moon_release(key);
    }
    
    // Merge additional options
    if (options && moon_is_dict(options)) {
        MoonValue* keys = moon_dict_keys(options);
        if (keys && moon_is_list(keys)) {
            int len = keys->data.listVal->length;
            for (int i = 0; i < len; i++) {
                MoonValue* key = keys->data.listVal->items[i];
                MoonValue* val = moon_dict_get(options, key, moon_null());
                moon_dict_set(opts, key, val);
            }
        }
        if (keys) moon_release(keys);
    }
    
    MoonValue* result = moon_gui_create(opts);
    moon_release(opts);
    return result;
}

// ============================================================================
// Window Control Functions
// ============================================================================

void moon_gui_show(MoonValue* win, MoonValue* show) {
    int winId = (int)moon_to_int(win);
    MoonWindow* window = GetWindowById(winId);
    if (!window) window = GetFirstWindow();
    if (!window) return;
    
    bool visible = moon_to_bool(show);
    if (visible) {
        if (window->webviewReady) {
            gtk_widget_show_all(window->window);
        } else {
            window->showPending = true;
        }
    } else {
        gtk_widget_hide(window->window);
    }
}

void moon_gui_set_title(MoonValue* win, MoonValue* title) {
    int winId = (int)moon_to_int(win);
    MoonWindow* window = GetWindowById(winId);
    if (!window) window = GetFirstWindow();
    if (!window || !moon_is_string(title)) return;
    
    gtk_window_set_title(GTK_WINDOW(window->window), title->data.strVal);
}

void moon_gui_set_size(MoonValue* win, MoonValue* w, MoonValue* h) {
    int winId = (int)moon_to_int(win);
    MoonWindow* window = GetWindowById(winId);
    if (!window) window = GetFirstWindow();
    if (!window) return;
    
    int width = (int)moon_to_int(w);
    int height = (int)moon_to_int(h);
    gtk_window_resize(GTK_WINDOW(window->window), width, height);
}

void moon_gui_set_position(MoonValue* win, MoonValue* x, MoonValue* y) {
    int winId = (int)moon_to_int(win);
    MoonWindow* window = GetWindowById(winId);
    if (!window) window = GetFirstWindow();
    if (!window) return;
    
    int px = (int)moon_to_int(x);
    int py = (int)moon_to_int(y);
    gtk_window_move(GTK_WINDOW(window->window), px, py);
}

void moon_gui_close(MoonValue* win) {
    int winId = (int)moon_to_int(win);
    MoonWindow* window = GetWindowById(winId);
    if (!window) window = GetFirstWindow();
    if (!window) return;
    
    gtk_widget_destroy(window->window);
}

// ============================================================================
// Window-Specific Functions (_win variants for multi-window support)
// ============================================================================

void moon_gui_show_win(MoonValue* winId, MoonValue* show) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    
    bool visible = moon_to_bool(show);
    if (visible) {
        if (window->webviewReady) {
            gtk_widget_show_all(window->window);
        } else {
            window->showPending = true;
        }
    } else {
        gtk_widget_hide(window->window);
    }
}

void moon_gui_set_title_win(MoonValue* winId, MoonValue* title) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(title)) return;
    gtk_window_set_title(GTK_WINDOW(window->window), title->data.strVal);
}

void moon_gui_set_size_win(MoonValue* winId, MoonValue* w, MoonValue* h) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_window_resize(GTK_WINDOW(window->window), (int)moon_to_int(w), (int)moon_to_int(h));
}

void moon_gui_set_position_win(MoonValue* winId, MoonValue* x, MoonValue* y) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_window_move(GTK_WINDOW(window->window), (int)moon_to_int(x), (int)moon_to_int(y));
}

void moon_gui_close_win(MoonValue* winId) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_widget_destroy(window->window);
}

void moon_gui_minimize_win(MoonValue* winId) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_window_iconify(GTK_WINDOW(window->window));
}

void moon_gui_maximize_win(MoonValue* winId) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_window_maximize(GTK_WINDOW(window->window));
}

void moon_gui_restore_win(MoonValue* winId) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    gtk_window_unmaximize(GTK_WINDOW(window->window));
    gtk_window_deiconify(GTK_WINDOW(window->window));
}

void moon_gui_on_message_win(MoonValue* winId, MoonValue* callback) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    
    if (window->messageCallback) {
        moon_release(window->messageCallback);
    }
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        window->messageCallback = callback;
    } else {
        window->messageCallback = nullptr;
    }
}

void moon_gui_on_close_win(MoonValue* winId, MoonValue* callback) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window) return;
    
    if (window->closeCallback) {
        moon_release(window->closeCallback);
    }
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        window->closeCallback = callback;
    } else {
        window->closeCallback = nullptr;
    }
}

// ============================================================================
// Content Loading
// ============================================================================

void moon_gui_load_url(MoonValue* url) {
    MoonWindow* window = GetFirstWindow();
    if (!window || !moon_is_string(url)) return;
    
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(window->webview), url->data.strVal);
}

void moon_gui_load_html(MoonValue* html) {
    MoonWindow* window = GetFirstWindow();
    if (!window || !moon_is_string(html)) return;
    
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(window->webview), html->data.strVal, "moon://localhost/");
}

void moon_gui_load_url_win(MoonValue* winId, MoonValue* url) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(url)) return;
    
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(window->webview), url->data.strVal);
}

void moon_gui_load_html_win(MoonValue* winId, MoonValue* html) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(html)) return;
    
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(window->webview), html->data.strVal, "moon://localhost/");
}

// ============================================================================
// JavaScript Interaction
// ============================================================================

static void on_js_finished(GObject* source, GAsyncResult* result, gpointer data) {
    GError* error = NULL;
    
#if USE_WEBKIT_EVALUATE_JAVASCRIPT
    JSCValue* jsValue = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
    
    if (error) {
        fprintf(stderr, "JavaScript error: %s\n", error->message);
        g_error_free(error);
    }
    
    if (jsValue) {
        g_object_unref(jsValue);
    }
#else
    WebKitJavascriptResult* jsResult = webkit_web_view_run_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
    
    if (error) {
        fprintf(stderr, "JavaScript error: %s\n", error->message);
        g_error_free(error);
    }
    
    if (jsResult) {
        webkit_javascript_result_unref(jsResult);
    }
#endif
}

MoonValue* moon_gui_eval_js(MoonValue* js) {
    MoonWindow* window = GetFirstWindow();
    if (!window || !moon_is_string(js)) return moon_null();
    
    moon_webkit_run_javascript(WEBKIT_WEB_VIEW(window->webview), 
                               js->data.strVal, on_js_finished, NULL);
    return moon_null();
}

MoonValue* moon_gui_eval_win(MoonValue* winId, MoonValue* js) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(js)) return moon_null();
    
    moon_webkit_run_javascript(WEBKIT_WEB_VIEW(window->webview), 
                               js->data.strVal, on_js_finished, NULL);
    return moon_null();
}

void moon_gui_post_message_win(MoonValue* winId, MoonValue* msg) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(msg)) return;
    
    // Escape the message for JavaScript
    const char* msgStr = msg->data.strVal;
    std::string escaped;
    for (const char* p = msgStr; *p; p++) {
        if (*p == '"') escaped += "\\\"";
        else if (*p == '\\') escaped += "\\\\";
        else if (*p == '\n') escaped += "\\n";
        else if (*p == '\r') escaped += "\\r";
        else if (*p == '\t') escaped += "\\t";
        else escaped += *p;
    }
    
    std::string script = "if(window.onMoonMessage) window.onMoonMessage(\"" + escaped + "\");";
    moon_webkit_run_javascript(WEBKIT_WEB_VIEW(window->webview), 
                               script.c_str(), NULL, NULL);
}

// Expose function to JavaScript (callable from JS)
void moon_gui_expose(MoonValue* name, MoonValue* callback) {
    MoonWindow* window = GetFirstWindow();
    if (!window || !moon_is_string(name)) return;
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        window->exposedFuncs[name->data.strVal] = callback;
    }
}

void moon_gui_expose_win(MoonValue* winId, MoonValue* name, MoonValue* callback) {
    MoonWindow* window = GetWindowById((int)moon_to_int(winId));
    if (!window || !moon_is_string(name)) return;
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        window->exposedFuncs[name->data.strVal] = callback;
    }
}

// ============================================================================
// Message Handling
// ============================================================================

void moon_gui_on_message(MoonValue* callback) {
    MoonWindow* window = GetFirstWindow();
    if (!window) return;
    
    if (window->messageCallback) {
        moon_release(window->messageCallback);
    }
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        window->messageCallback = callback;
    } else {
        window->messageCallback = nullptr;
    }
}

// ============================================================================
// Message Loop
// ============================================================================

void moon_gui_run(void) {
    if (!g_gui_initialized || g_windows.empty()) return;
    
    g_running = true;
    gtk_main();
    g_running = false;
}

void moon_gui_quit(void) {
    if (g_running) {
        gtk_main_quit();
        g_running = false;
    }
}

// ============================================================================
// Dialog Functions
// ============================================================================

MoonValue* moon_gui_alert(MoonValue* msg) {
    if (!moon_is_string(msg)) return moon_null();
    
    GtkWidget* dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s",
        msg->data.strVal
    );
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    return moon_null();
}

MoonValue* moon_gui_confirm(MoonValue* msg) {
    if (!moon_is_string(msg)) return moon_bool(false);
    
    GtkWidget* dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s",
        msg->data.strVal
    );
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    return moon_bool(result == GTK_RESPONSE_YES);
}

// ============================================================================
// System Tray (AppIndicator)
// ============================================================================

#ifdef HAVE_APPINDICATOR
static void on_tray_menu_item_activate(GtkMenuItem* item, gpointer data) {
    int index = GPOINTER_TO_INT(data);
    
    if (g_trayCallback) {
        MoonValue* args[1] = { moon_int(index) };
        MoonValue* result = moon_call_func(g_trayCallback, args, 1);
        if (result) moon_release(result);
        moon_release(args[0]);
    }
}
#endif

MoonValue* moon_gui_tray_create(MoonValue* tooltip, MoonValue* iconPath) {
#ifdef HAVE_APPINDICATOR
    if (g_indicator) return moon_bool(true);
    
    const char* icon = moon_is_string(iconPath) ? iconPath->data.strVal : "application-default-icon";
    
    g_indicator = app_indicator_new(
        "moonscript-app",
        icon,
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );
    
    app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
    
    if (moon_is_string(tooltip)) {
        app_indicator_set_title(g_indicator, tooltip->data.strVal);
    }
    
    // Create empty menu (required by AppIndicator)
    g_trayMenu = gtk_menu_new();
    app_indicator_set_menu(g_indicator, GTK_MENU(g_trayMenu));
    
    return moon_bool(true);
#else
    fprintf(stderr, "Warning: System tray not available (libappindicator not installed)\n");
    return moon_bool(false);
#endif
}

void moon_gui_tray_remove(void) {
#ifdef HAVE_APPINDICATOR
    if (g_indicator) {
        g_object_unref(g_indicator);
        g_indicator = NULL;
    }
    if (g_trayMenu) {
        gtk_widget_destroy(g_trayMenu);
        g_trayMenu = NULL;
    }
#endif
}

MoonValue* moon_gui_tray_set_menu(MoonValue* items) {
#ifdef HAVE_APPINDICATOR
    if (!g_indicator || !moon_is_list(items)) return moon_bool(false);
    
    // Clear existing menu
    if (g_trayMenu) {
        gtk_widget_destroy(g_trayMenu);
    }
    g_trayMenu = gtk_menu_new();
    
    MoonList* list = items->data.listVal;
    for (int i = 0; i < list->length; i++) {
        MoonValue* item = list->items[i];
        
        if (moon_is_string(item)) {
            const char* label = item->data.strVal;
            
            if (strcmp(label, "-") == 0 || strcmp(label, "---") == 0) {
                // Separator
                GtkWidget* separator = gtk_separator_menu_item_new();
                gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), separator);
            } else {
                // Menu item
                GtkWidget* menuItem = gtk_menu_item_new_with_label(label);
                g_signal_connect(menuItem, "activate",
                                 G_CALLBACK(on_tray_menu_item_activate),
                                 GINT_TO_POINTER(i));
                gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), menuItem);
            }
        }
    }
    
    gtk_widget_show_all(g_trayMenu);
    app_indicator_set_menu(g_indicator, GTK_MENU(g_trayMenu));
    
    return moon_bool(true);
#else
    return moon_bool(false);
#endif
}

void moon_gui_tray_on_click(MoonValue* callback) {
    if (g_trayCallback) {
        moon_release(g_trayCallback);
    }
    
    if (callback && callback->type == MOON_FUNC) {
        moon_retain(callback);
        g_trayCallback = callback;
    } else {
        g_trayCallback = nullptr;
    }
}

void moon_gui_show_window(MoonValue* show) {
    MoonWindow* window = GetFirstWindow();
    if (!window) return;
    
    if (moon_to_bool(show)) {
        gtk_widget_show_all(window->window);
        gtk_window_present(GTK_WINDOW(window->window));
    } else {
        gtk_widget_hide(window->window);
    }
}

#else // !MOON_PLATFORM_LINUX || MOON_NO_GUI

// ============================================================================
// Stub implementations for non-Linux platforms or when GUI is disabled
// ============================================================================

MoonValue* moon_gui_init(void) { return moon_bool(false); }
MoonValue* moon_gui_create(MoonValue* options) { return moon_int(-1); }
MoonValue* moon_gui_create_advanced(MoonValue* title, MoonValue* width, MoonValue* height, MoonValue* options) { return moon_int(-1); }

// Basic window functions
void moon_gui_show(MoonValue* win, MoonValue* show) {}
void moon_gui_set_title(MoonValue* win, MoonValue* title) {}
void moon_gui_set_size(MoonValue* win, MoonValue* w, MoonValue* h) {}
void moon_gui_set_position(MoonValue* win, MoonValue* x, MoonValue* y) {}
void moon_gui_close(MoonValue* win) {}

// Window-specific functions (_win variants)
void moon_gui_show_win(MoonValue* winId, MoonValue* show) {}
void moon_gui_set_title_win(MoonValue* winId, MoonValue* title) {}
void moon_gui_set_size_win(MoonValue* winId, MoonValue* w, MoonValue* h) {}
void moon_gui_set_position_win(MoonValue* winId, MoonValue* x, MoonValue* y) {}
void moon_gui_close_win(MoonValue* winId) {}
void moon_gui_minimize_win(MoonValue* winId) {}
void moon_gui_maximize_win(MoonValue* winId) {}
void moon_gui_restore_win(MoonValue* winId) {}
void moon_gui_on_message_win(MoonValue* winId, MoonValue* callback) {}
void moon_gui_on_close_win(MoonValue* winId, MoonValue* callback) {}

// Content loading
void moon_gui_load_url(MoonValue* url) {}
void moon_gui_load_html(MoonValue* html) {}
void moon_gui_load_url_win(MoonValue* winId, MoonValue* url) {}
void moon_gui_load_html_win(MoonValue* winId, MoonValue* html) {}

// JavaScript interaction
MoonValue* moon_gui_eval_js(MoonValue* js) { return moon_null(); }
MoonValue* moon_gui_eval_win(MoonValue* winId, MoonValue* js) { return moon_null(); }
void moon_gui_post_message_win(MoonValue* winId, MoonValue* msg) {}
void moon_gui_expose(MoonValue* name, MoonValue* callback) {}
void moon_gui_expose_win(MoonValue* winId, MoonValue* name, MoonValue* callback) {}

// Message handling
void moon_gui_on_message(MoonValue* callback) {}

// Message loop
void moon_gui_run(void) {}
void moon_gui_quit(void) {}

// Dialogs
MoonValue* moon_gui_alert(MoonValue* msg) { return moon_null(); }
MoonValue* moon_gui_confirm(MoonValue* msg) { return moon_bool(false); }

// System tray
MoonValue* moon_gui_tray_create(MoonValue* tooltip, MoonValue* iconPath) { return moon_bool(false); }
void moon_gui_tray_remove(void) {}
MoonValue* moon_gui_tray_set_menu(MoonValue* items) { return moon_bool(false); }
void moon_gui_tray_on_click(MoonValue* callback) {}
void moon_gui_show_window(MoonValue* show) {}

#endif // MOON_PLATFORM_LINUX && !MOON_NO_GUI
