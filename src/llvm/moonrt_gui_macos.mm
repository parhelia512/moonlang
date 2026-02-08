// MoonLang Runtime - GUI Support for macOS (AppKit + WKWebView)
// Copyright (c) 2026 greenteng.com
//
// macOS GUI implementation using Cocoa AppKit and WebKit
// Provides the same API as the Windows WebView2 and Linux GTK versions

#include "moonrt.h"
#include "moonrt_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Only compile on macOS and when GUI is not disabled
#if defined(MOON_PLATFORM_MACOS) && !defined(MOON_NO_GUI)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include <map>
#include <string>
#include <unordered_map>

// ============================================================================
// Forward Declarations
// ============================================================================

@class MoonWindowDelegate;
@class MoonWebViewMessageHandler;

// ============================================================================
// MoonWindow Structure (Multi-Window Support)
// ============================================================================

struct MoonWindow {
    int id;                              // Unique window ID
    NSWindow* window;                    // NSWindow
    WKWebView* webview;                  // WKWebView
    MoonWindowDelegate* delegate;        // Window delegate
    MoonWebViewMessageHandler* messageHandler;
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
    
    // Pending content
    char* pendingHtml;
    char* pendingUrl;
    
    // Parent window
    int parentId;
    bool modal;
    
    MoonWindow() : id(0), window(nil), webview(nil), delegate(nil), messageHandler(nil),
        webviewReady(false), showPending(false), frameless(false), transparent(false),
        topmost(false), resizable(true), clickThrough(false), devtools(false), alpha(255),
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

// System tray (Status bar item)
static NSStatusItem* g_statusItem = nil;
static NSMenu* g_trayMenu = nil;
static MoonValue* g_trayCallback = nullptr;

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

static NSString* UTF8ToNSString(const char* str) {
    return str ? [NSString stringWithUTF8String:str] : @"";
}

// ============================================================================
// MoonWebViewMessageHandler - Handle JS messages
// ============================================================================

@interface MoonWebViewMessageHandler : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) MoonWindow* moonWindow;
@end

@implementation MoonWebViewMessageHandler

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (!self.moonWindow) return;
    
    if ([message.body isKindOfClass:[NSString class]]) {
        NSString* body = (NSString*)message.body;
        const char* utf8 = [body UTF8String];
        
        // Handle special messages
        if (strcmp(utf8, "__drag__") == 0) {
            [self.moonWindow->window performWindowDragWithEvent:[NSApp currentEvent]];
            return;
        } else if (strcmp(utf8, "__close__") == 0) {
            [self.moonWindow->window close];
            return;
        } else if (strcmp(utf8, "__minimize__") == 0) {
            [self.moonWindow->window miniaturize:nil];
            return;
        } else if (strcmp(utf8, "__maximize__") == 0) {
            if ([self.moonWindow->window isZoomed]) {
                [self.moonWindow->window zoom:nil];
            } else {
                [self.moonWindow->window zoom:nil];
            }
            return;
        }
        
        // Regular message callback
        if (self.moonWindow->messageCallback) {
            MoonValue* args[2] = { moon_int(self.moonWindow->id), moon_string(utf8) };
            MoonValue* result = moon_call_func(self.moonWindow->messageCallback, args, 2);
            if (result) moon_release(result);
            moon_release(args[0]);
            moon_release(args[1]);
        }
    }
}

@end

// ============================================================================
// MoonWindowDelegate - Handle window events
// ============================================================================

@interface MoonWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) MoonWindow* moonWindow;
@end

@implementation MoonWindowDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (!self.moonWindow) return YES;
    
    if (self.moonWindow->closeCallback) {
        MoonValue* args[1] = { moon_int(self.moonWindow->id) };
        MoonValue* result = moon_call_func(self.moonWindow->closeCallback, args, 1);
        bool shouldClose = true;
        if (result) {
            shouldClose = moon_to_bool(result);
            moon_release(result);
        }
        moon_release(args[0]);
        if (!shouldClose) return NO;
    }
    
    // Remove from windows map
    g_windows.erase(self.moonWindow->id);
    delete self.moonWindow;
    
    // Quit if no windows left
    if (g_windows.empty() && g_statusItem == nil) {
        [NSApp terminate:nil];
        g_running = false;
    }
    
    return YES;
}

@end

// ============================================================================
// WebView Navigation Delegate
// ============================================================================

@interface MoonWebViewNavigationDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) MoonWindow* moonWindow;
@end

@implementation MoonWebViewNavigationDelegate

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    if (!self.moonWindow) return;
    
    self.moonWindow->webviewReady = true;
    
    // Inject MoonGUI JS API
    NSString* script = [NSString stringWithFormat:@""
        "window.__moonCallId = 0;"
        "window.__moonCallResults = {};"
        "window.MoonGUI = {"
        "  windowId: %d,"
        "  send: function(msg) { window.webkit.messageHandlers.moonMessage.postMessage(msg); },"
        "  close: function() { window.webkit.messageHandlers.moonMessage.postMessage('__close__'); },"
        "  minimize: function() { window.webkit.messageHandlers.moonMessage.postMessage('__minimize__'); },"
        "  maximize: function() { window.webkit.messageHandlers.moonMessage.postMessage('__maximize__'); },"
        "  drag: function() { window.webkit.messageHandlers.moonMessage.postMessage('__drag__'); }"
        "};"
        "window.chrome = window.chrome || {};"
        "window.chrome.webview = window.chrome.webview || {"
        "  postMessage: function(msg) { window.MoonGUI.send(msg); }"
        "};", self.moonWindow->id];
    
    [webView evaluateJavaScript:script completionHandler:nil];
    
    // Show window if pending
    if (self.moonWindow->showPending) {
        [self.moonWindow->window makeKeyAndOrderFront:nil];
        self.moonWindow->showPending = false;
    }
}

@end

// ============================================================================
// URL Scheme Handler for moon:// protocol
// ============================================================================

@interface MoonURLSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation MoonURLSchemeHandler

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    NSURL* url = urlSchemeTask.request.URL;
    NSString* path = url.path;
    
    // Normalize path
    if ([path hasPrefix:@"/"]) {
        path = [path substringFromIndex:1];
    }
    std::string pathStr = [path.lowercaseString UTF8String];
    
    // Look up in virtual file system
    auto it = g_virtualFiles.find(pathStr);
    if (it != g_virtualFiles.end()) {
        const std::string& content = it->second;
        NSData* data = [NSData dataWithBytes:content.data() length:content.size()];
        
        // Determine MIME type
        NSString* mimeType = @"application/octet-stream";
        if ([path hasSuffix:@".html"] || [path hasSuffix:@".htm"]) mimeType = @"text/html";
        else if ([path hasSuffix:@".css"]) mimeType = @"text/css";
        else if ([path hasSuffix:@".js"]) mimeType = @"application/javascript";
        else if ([path hasSuffix:@".json"]) mimeType = @"application/json";
        else if ([path hasSuffix:@".png"]) mimeType = @"image/png";
        else if ([path hasSuffix:@".jpg"] || [path hasSuffix:@".jpeg"]) mimeType = @"image/jpeg";
        else if ([path hasSuffix:@".gif"]) mimeType = @"image/gif";
        else if ([path hasSuffix:@".svg"]) mimeType = @"image/svg+xml";
        
        NSURLResponse* response = [[NSURLResponse alloc] initWithURL:url
                                                            MIMEType:mimeType
                                               expectedContentLength:data.length
                                                    textEncodingName:@"utf-8"];
        [urlSchemeTask didReceiveResponse:response];
        [urlSchemeTask didReceiveData:data];
        [urlSchemeTask didFinish];
    } else {
        NSError* error = [NSError errorWithDomain:NSURLErrorDomain
                                             code:NSURLErrorFileDoesNotExist
                                         userInfo:nil];
        [urlSchemeTask didFailWithError:error];
    }
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    // Nothing to do
}

@end

static MoonURLSchemeHandler* g_schemeHandler = nil;
static MoonWebViewNavigationDelegate* g_navigationDelegate = nil;

// ============================================================================
// GUI Initialization
// ============================================================================

extern "C" {

MoonValue* moon_gui_init(void) {
    if (g_gui_initialized) return moon_bool(true);
    
    @autoreleasepool {
        // Initialize NSApplication
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        
        // Create scheme handler
        g_schemeHandler = [[MoonURLSchemeHandler alloc] init];
        g_navigationDelegate = [[MoonWebViewNavigationDelegate alloc] init];
        
        g_gui_initialized = true;
    }
    
    return moon_bool(true);
}

// ============================================================================
// Window Creation
// ============================================================================

MoonValue* moon_gui_create(MoonValue* options) {
    if (!g_gui_initialized) {
        moon_gui_init();
    }
    
    @autoreleasepool {
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
        
        // Window style mask
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled |
                                      NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskMiniaturizable;
        
        if (win->resizable) {
            styleMask |= NSWindowStyleMaskResizable;
        }
        
        if (win->frameless) {
            styleMask = NSWindowStyleMaskBorderless;
            if (win->resizable) {
                styleMask |= NSWindowStyleMaskResizable;
            }
        }
        
        // Create window
        NSRect frame = NSMakeRect(0, 0, width, height);
        win->window = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:styleMask
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        
        [win->window setTitle:UTF8ToNSString(title)];
        
        // Window options
        if (win->topmost) {
            [win->window setLevel:NSFloatingWindowLevel];
        }
        
        if (win->transparent) {
            [win->window setOpaque:NO];
            [win->window setBackgroundColor:[NSColor clearColor]];
        }
        
        if (win->alpha < 255) {
            [win->window setAlphaValue:win->alpha / 255.0];
        }
        
        // Position
        if (center) {
            [win->window center];
        } else if (x >= 0 && y >= 0) {
            // macOS uses bottom-left origin, convert from top-left
            NSScreen* screen = [NSScreen mainScreen];
            CGFloat screenHeight = screen.frame.size.height;
            [win->window setFrameTopLeftPoint:NSMakePoint(x, screenHeight - y)];
        }
        
        // Create WKWebView configuration
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        WKUserContentController* userContentController = [[WKUserContentController alloc] init];
        
        // Set up message handler
        win->messageHandler = [[MoonWebViewMessageHandler alloc] init];
        win->messageHandler.moonWindow = win;
        [userContentController addScriptMessageHandler:win->messageHandler name:@"moonMessage"];
        
        config.userContentController = userContentController;
        
        // Register moon:// scheme handler
        [config setURLSchemeHandler:g_schemeHandler forURLScheme:@"moon"];
        
        // Enable developer tools if requested
        if (win->devtools) {
            [config.preferences setValue:@YES forKey:@"developerExtrasEnabled"];
        }
        
        // Create WebView
        win->webview = [[WKWebView alloc] initWithFrame:frame configuration:config];
        
        // Set up navigation delegate
        MoonWebViewNavigationDelegate* navDelegate = [[MoonWebViewNavigationDelegate alloc] init];
        navDelegate.moonWindow = win;
        win->webview.navigationDelegate = navDelegate;
        
        // Transparent WebView background
        if (win->transparent) {
            [win->webview setValue:@NO forKey:@"drawsBackground"];
        }
        
        // Add WebView to window
        [win->window setContentView:win->webview];
        
        // Set up window delegate
        win->delegate = [[MoonWindowDelegate alloc] init];
        win->delegate.moonWindow = win;
        [win->window setDelegate:win->delegate];
        
        // Store window
        g_windows[win->id] = win;
        
        return moon_int(win->id);
    }
}

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
    @autoreleasepool {
        int winId = (int)moon_to_int(win);
        MoonWindow* window = GetWindowById(winId);
        if (!window) window = GetFirstWindow();
        if (!window) return;
        
        bool visible = moon_to_bool(show);
        if (visible) {
            if (window->webviewReady) {
                [window->window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            } else {
                window->showPending = true;
            }
        } else {
            [window->window orderOut:nil];
        }
    }
}

void moon_gui_set_title(MoonValue* win, MoonValue* title) {
    @autoreleasepool {
        int winId = (int)moon_to_int(win);
        MoonWindow* window = GetWindowById(winId);
        if (!window) window = GetFirstWindow();
        if (!window || !moon_is_string(title)) return;
        
        [window->window setTitle:UTF8ToNSString(title->data.strVal)];
    }
}

void moon_gui_set_size(MoonValue* win, MoonValue* w, MoonValue* h) {
    @autoreleasepool {
        int winId = (int)moon_to_int(win);
        MoonWindow* window = GetWindowById(winId);
        if (!window) window = GetFirstWindow();
        if (!window) return;
        
        int width = (int)moon_to_int(w);
        int height = (int)moon_to_int(h);
        NSRect frame = [window->window frame];
        frame.size.width = width;
        frame.size.height = height;
        [window->window setFrame:frame display:YES];
    }
}

void moon_gui_set_position(MoonValue* win, MoonValue* x, MoonValue* y) {
    @autoreleasepool {
        int winId = (int)moon_to_int(win);
        MoonWindow* window = GetWindowById(winId);
        if (!window) window = GetFirstWindow();
        if (!window) return;
        
        int px = (int)moon_to_int(x);
        int py = (int)moon_to_int(y);
        
        // macOS uses bottom-left origin, convert from top-left
        NSScreen* screen = [NSScreen mainScreen];
        CGFloat screenHeight = screen.frame.size.height;
        [window->window setFrameTopLeftPoint:NSMakePoint(px, screenHeight - py)];
    }
}

void moon_gui_close(MoonValue* win) {
    @autoreleasepool {
        int winId = (int)moon_to_int(win);
        MoonWindow* window = GetWindowById(winId);
        if (!window) window = GetFirstWindow();
        if (!window) return;
        
        [window->window close];
    }
}

// ============================================================================
// Window-Specific Functions (_win variants)
// ============================================================================

void moon_gui_show_win(MoonValue* winId, MoonValue* show) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        
        bool visible = moon_to_bool(show);
        if (visible) {
            if (window->webviewReady) {
                [window->window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            } else {
                window->showPending = true;
            }
        } else {
            [window->window orderOut:nil];
        }
    }
}

void moon_gui_set_title_win(MoonValue* winId, MoonValue* title) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window || !moon_is_string(title)) return;
        [window->window setTitle:UTF8ToNSString(title->data.strVal)];
    }
}

void moon_gui_set_size_win(MoonValue* winId, MoonValue* w, MoonValue* h) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        NSRect frame = [window->window frame];
        frame.size.width = (int)moon_to_int(w);
        frame.size.height = (int)moon_to_int(h);
        [window->window setFrame:frame display:YES];
    }
}

void moon_gui_set_position_win(MoonValue* winId, MoonValue* x, MoonValue* y) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        NSScreen* screen = [NSScreen mainScreen];
        CGFloat screenHeight = screen.frame.size.height;
        [window->window setFrameTopLeftPoint:NSMakePoint((int)moon_to_int(x), screenHeight - (int)moon_to_int(y))];
    }
}

void moon_gui_close_win(MoonValue* winId) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        [window->window close];
    }
}

void moon_gui_minimize_win(MoonValue* winId) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        [window->window miniaturize:nil];
    }
}

void moon_gui_maximize_win(MoonValue* winId) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        [window->window zoom:nil];
    }
}

void moon_gui_restore_win(MoonValue* winId) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window) return;
        [window->window deminiaturize:nil];
    }
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
    @autoreleasepool {
        MoonWindow* window = GetFirstWindow();
        if (!window || !moon_is_string(url)) return;
        
        NSURL* nsurl = [NSURL URLWithString:UTF8ToNSString(url->data.strVal)];
        NSURLRequest* request = [NSURLRequest requestWithURL:nsurl];
        [window->webview loadRequest:request];
    }
}

void moon_gui_load_html(MoonValue* html) {
    @autoreleasepool {
        MoonWindow* window = GetFirstWindow();
        if (!window || !moon_is_string(html)) return;
        
        [window->webview loadHTMLString:UTF8ToNSString(html->data.strVal)
                                baseURL:[NSURL URLWithString:@"moon://localhost/"]];
    }
}

void moon_gui_load_url_win(MoonValue* winId, MoonValue* url) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window || !moon_is_string(url)) return;
        
        NSURL* nsurl = [NSURL URLWithString:UTF8ToNSString(url->data.strVal)];
        NSURLRequest* request = [NSURLRequest requestWithURL:nsurl];
        [window->webview loadRequest:request];
    }
}

void moon_gui_load_html_win(MoonValue* winId, MoonValue* html) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window || !moon_is_string(html)) return;
        
        [window->webview loadHTMLString:UTF8ToNSString(html->data.strVal)
                                baseURL:[NSURL URLWithString:@"moon://localhost/"]];
    }
}

// ============================================================================
// JavaScript Interaction
// ============================================================================

MoonValue* moon_gui_eval_js(MoonValue* js) {
    @autoreleasepool {
        MoonWindow* window = GetFirstWindow();
        if (!window || !moon_is_string(js)) return moon_null();
        
        [window->webview evaluateJavaScript:UTF8ToNSString(js->data.strVal)
                          completionHandler:nil];
        return moon_null();
    }
}

MoonValue* moon_gui_eval_win(MoonValue* winId, MoonValue* js) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window || !moon_is_string(js)) return moon_null();
        
        [window->webview evaluateJavaScript:UTF8ToNSString(js->data.strVal)
                          completionHandler:nil];
        return moon_null();
    }
}

void moon_gui_post_message_win(MoonValue* winId, MoonValue* msg) {
    @autoreleasepool {
        MoonWindow* window = GetWindowById((int)moon_to_int(winId));
        if (!window || !moon_is_string(msg)) return;
        
        // Escape the message for JavaScript
        const char* msgStr = msg->data.strVal;
        NSMutableString* escaped = [NSMutableString string];
        for (const char* p = msgStr; *p; p++) {
            if (*p == '"') [escaped appendString:@"\\\""];
            else if (*p == '\\') [escaped appendString:@"\\\\"];
            else if (*p == '\n') [escaped appendString:@"\\n"];
            else if (*p == '\r') [escaped appendString:@"\\r"];
            else if (*p == '\t') [escaped appendString:@"\\t"];
            else [escaped appendFormat:@"%c", *p];
        }
        
        NSString* script = [NSString stringWithFormat:
            @"if(window.onMoonMessage) window.onMoonMessage(\"%@\");", escaped];
        [window->webview evaluateJavaScript:script completionHandler:nil];
    }
}

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
    @autoreleasepool {
        if (!g_gui_initialized || g_windows.empty()) return;
        
        // Show all windows that haven't been shown yet
        for (auto& pair : g_windows) {
            MoonWindow* win = pair.second;
            if (win && win->window) {
                [win->window makeKeyAndOrderFront:nil];
            }
        }
        
        // Activate the application (bring to front)
        [NSApp activateIgnoringOtherApps:YES];
        
        g_running = true;
        [NSApp run];
        g_running = false;
    }
}

void moon_gui_quit(void) {
    @autoreleasepool {
        if (g_running) {
            [NSApp terminate:nil];
            g_running = false;
        }
    }
}

// ============================================================================
// Dialog Functions
// ============================================================================

MoonValue* moon_gui_alert(MoonValue* msg) {
    @autoreleasepool {
        if (!moon_is_string(msg)) return moon_null();
        
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:UTF8ToNSString(msg->data.strVal)];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        
        return moon_null();
    }
}

MoonValue* moon_gui_confirm(MoonValue* msg) {
    @autoreleasepool {
        if (!moon_is_string(msg)) return moon_bool(false);
        
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:UTF8ToNSString(msg->data.strVal)];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        
        NSModalResponse response = [alert runModal];
        return moon_bool(response == NSAlertFirstButtonReturn);
    }
}

// ============================================================================
// System Tray (Status Bar Item)
// ============================================================================

@interface MoonTrayDelegate : NSObject
@end

@implementation MoonTrayDelegate

- (void)trayClicked:(id)sender {
    if (g_trayCallback) {
        MoonValue* args[1] = { moon_string("click") };
        MoonValue* result = moon_call_func(g_trayCallback, args, 1);
        if (result) moon_release(result);
        moon_release(args[0]);
    }
}

- (void)menuItemClicked:(NSMenuItem*)sender {
    if (g_trayCallback) {
        const char* title = [[sender title] UTF8String];
        MoonValue* args[1] = { moon_string(title) };
        MoonValue* result = moon_call_func(g_trayCallback, args, 1);
        if (result) moon_release(result);
        moon_release(args[0]);
    }
}

@end

static MoonTrayDelegate* g_trayDelegate = nil;

MoonValue* moon_gui_tray_create(MoonValue* tooltip, MoonValue* iconPath) {
    @autoreleasepool {
        if (g_statusItem) return moon_bool(true);
        
        g_statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        
        if (!g_trayDelegate) {
            g_trayDelegate = [[MoonTrayDelegate alloc] init];
        }
        
        // Set icon (use system icon if not specified)
        if (moon_is_string(iconPath) && strlen(iconPath->data.strVal) > 0) {
            NSImage* icon = [[NSImage alloc] initWithContentsOfFile:UTF8ToNSString(iconPath->data.strVal)];
            if (icon) {
                [icon setSize:NSMakeSize(18, 18)];
                [g_statusItem.button setImage:icon];
            }
        } else {
            // Default icon
            [g_statusItem.button setTitle:@"🌙"];
        }
        
        // Set tooltip
        if (moon_is_string(tooltip)) {
            [g_statusItem.button setToolTip:UTF8ToNSString(tooltip->data.strVal)];
        }
        
        // Set click action
        [g_statusItem.button setTarget:g_trayDelegate];
        [g_statusItem.button setAction:@selector(trayClicked:)];
        
        return moon_bool(true);
    }
}

void moon_gui_tray_remove(void) {
    @autoreleasepool {
        if (g_statusItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:g_statusItem];
            g_statusItem = nil;
        }
        if (g_trayMenu) {
            g_trayMenu = nil;
        }
    }
}

MoonValue* moon_gui_tray_set_menu(MoonValue* items) {
    @autoreleasepool {
        if (!g_statusItem || !moon_is_list(items)) return moon_bool(false);
        
        g_trayMenu = [[NSMenu alloc] init];
        
        MoonList* list = items->data.listVal;
        for (int i = 0; i < list->length; i++) {
            MoonValue* item = list->items[i];
            
            if (moon_is_string(item)) {
                const char* label = item->data.strVal;
                
                if (strcmp(label, "-") == 0 || strcmp(label, "---") == 0) {
                    [g_trayMenu addItem:[NSMenuItem separatorItem]];
                } else {
                    NSMenuItem* menuItem = [[NSMenuItem alloc]
                        initWithTitle:UTF8ToNSString(label)
                               action:@selector(menuItemClicked:)
                        keyEquivalent:@""];
                    [menuItem setTarget:g_trayDelegate];
                    [g_trayMenu addItem:menuItem];
                }
            }
        }
        
        [g_statusItem setMenu:g_trayMenu];
        
        return moon_bool(true);
    }
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
    @autoreleasepool {
        MoonWindow* window = GetFirstWindow();
        if (!window) return;
        
        if (moon_to_bool(show)) {
            [window->window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        } else {
            [window->window orderOut:nil];
        }
    }
}

} // extern "C"

#else // !MOON_PLATFORM_MACOS || MOON_NO_GUI

// ============================================================================
// Stub implementations for non-macOS platforms or when GUI is disabled
// ============================================================================

extern "C" {

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

} // extern "C"

#endif // MOON_PLATFORM_MACOS && !MOON_NO_GUI
