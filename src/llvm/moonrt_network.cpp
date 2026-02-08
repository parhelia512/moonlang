// MoonLang Runtime - Network Module
// Copyright (c) 2026 greenteng.com
//
// TCP/UDP network operations (conditionally compiled).
// Windows: IOCP/WSAPoll for high concurrency
// Linux: epoll for high concurrency

#include "moonrt_core.h"

#ifdef MOON_HAS_NETWORK

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")

static bool g_wsaInitialized = false;

// IOCP handle for high-concurrency operations
static HANDLE g_iocp = NULL;
static const int IOCP_MAX_EVENTS = 1024;

// Per-socket IOCP context
typedef struct {
    OVERLAPPED overlapped;
    SOCKET socket;
    int operation;  // 0=read, 1=write, 2=accept
    char buffer[4096];
    WSABUF wsaBuf;
    int index;      // Original socket index for select results
} IocpContext;

static void init_wsa() {
    if (!g_wsaInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        g_wsaInitialized = true;
        // Create global IOCP handle for high-concurrency operations
        g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    }
}

#define MOON_SOCKET SOCKET
#define socklen_t int

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>

#ifdef __linux__
#include <sys/epoll.h>
#endif

#ifdef __APPLE__
#include <sys/event.h>  // for kqueue
#endif

#define SOCKET int
#define MOON_SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
static void init_wsa() {}
#endif

// ============================================================================
// TCP Functions
// ============================================================================

MoonValue* moon_tcp_connect(MoonValue* host, MoonValue* port) {
    init_wsa();
    if (!moon_is_string(host)) return moon_int(-1);
    
    MOON_SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return moon_int(-1);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)moon_to_int(port));
    
    struct hostent* he = gethostbyname(host->data.strVal);
    if (!he) {
        closesocket(sock);
        return moon_int(-1);
    }
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return moon_int(-1);
    }
    
    return moon_int((int64_t)sock);
}

MoonValue* moon_tcp_listen(MoonValue* port) {
    init_wsa();
    
    MOON_SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return moon_int(-1);
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)moon_to_int(port));
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return moon_int(-1);
    }
    
    if (listen(sock, 10) == SOCKET_ERROR) {
        closesocket(sock);
        return moon_int(-1);
    }
    
    return moon_int((int64_t)sock);
}

MoonValue* moon_tcp_accept(MoonValue* server) {
    MOON_SOCKET serverSock = (MOON_SOCKET)moon_to_int(server);
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    MOON_SOCKET clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSock == INVALID_SOCKET) return moon_int(-1);
    
    // Use thread-safe inet_ntop instead of inet_ntoa
    char addrBuf[INET_ADDRSTRLEN];
#ifdef _WIN32
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
#else
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
#endif
    
    MoonValue* result = moon_dict_new();
    MoonValue* sockKey = moon_string("socket");
    MoonValue* sockVal = moon_int((int64_t)clientSock);
    MoonValue* addrKey = moon_string("address");
    MoonValue* addrVal = moon_string(addrBuf);
    MoonValue* portKey = moon_string("port");
    MoonValue* portVal = moon_int(ntohs(clientAddr.sin_port));
    
    moon_dict_set(result, sockKey, sockVal);
    moon_dict_set(result, addrKey, addrVal);
    moon_dict_set(result, portKey, portVal);
    
    moon_release(sockKey);
    moon_release(sockVal);
    moon_release(addrKey);
    moon_release(addrVal);
    moon_release(portKey);
    moon_release(portVal);
    
    return result;
}

MoonValue* moon_tcp_send(MoonValue* socket, MoonValue* data) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    
    // For string type, use its actual length (supports binary data)
    int len = 0;
    const char* str = NULL;
    char* toFree = NULL;
    
    if (data && data->type == MOON_STRING && data->data.strVal) {
        str = data->data.strVal;
        MoonStrHeader* header = moon_str_get_header(data->data.strVal);
        if (header) {
            len = (int)header->length;
        } else {
            len = (int)strlen(str);
        }
    } else {
        toFree = moon_to_string(data);
        str = toFree;
        len = (int)strlen(str);
    }
    
    int sent = send(sock, str, len, 0);
    
    if (toFree) {
        free(toFree);
    }
    
    return moon_int(sent);
}

MoonValue* moon_tcp_recv(MoonValue* socket) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    char buffer[4096];
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return moon_string("");
    buffer[received] = '\0';
    
    // Create string with explicit length (supports binary data including \\0)
    char* result = moon_str_with_capacity(buffer, received, received);
    MoonStrHeader* header = moon_str_get_header(result);
    if (header) {
        header->length = received;
    }
    return moon_string_owned(result);
}

void moon_tcp_close(MoonValue* socket) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    closesocket(sock);
}

// ============================================================================
// Async I/O Support
// ============================================================================

#ifdef _WIN32

MoonValue* moon_tcp_set_nonblocking(MoonValue* socket, MoonValue* nonblocking) {
    SOCKET sock = (SOCKET)moon_to_int(socket);
    u_long mode = moon_to_bool(nonblocking) ? 1 : 0;
    int result = ioctlsocket(sock, FIONBIO, &mode);
    return moon_bool(result == 0);
}

MoonValue* moon_tcp_has_data(MoonValue* socket) {
    SOCKET sock = (SOCKET)moon_to_int(socket);
    // Use WSAPoll for single socket check (more efficient than select)
    WSAPOLLFD pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int result = WSAPoll(&pfd, 1, 0);
    return moon_bool(result > 0 && (pfd.revents & POLLIN));
}

// High-concurrency tcp_select using WSAPoll (no 64-socket limit like select)
MoonValue* moon_tcp_select(MoonValue* sockets, MoonValue* timeout_ms, MoonValue* mode) {
    if (!moon_is_list(sockets)) return moon_list_new();
    
    MoonList* sockList = sockets->data.listVal;
    if (sockList->length == 0) return moon_list_new();
    
    const char* modeStr = moon_is_string(mode) ? mode->data.strVal : "read";
    bool checkRead = (strcmp(modeStr, "read") == 0 || strcmp(modeStr, "both") == 0);
    bool checkWrite = (strcmp(modeStr, "write") == 0 || strcmp(modeStr, "both") == 0);
    
    int64_t ms = moon_to_int(timeout_ms);
    MoonValue* readyList = moon_list_new();
    
    // Use WSAPoll for high concurrency (supports thousands of sockets)
    WSAPOLLFD* pfds = (WSAPOLLFD*)malloc(sockList->length * sizeof(WSAPOLLFD));
    if (!pfds) return readyList;
    
    for (int i = 0; i < sockList->length; i++) {
        pfds[i].fd = (SOCKET)moon_to_int(sockList->items[i]);
        pfds[i].events = 0;
        if (checkRead) pfds[i].events |= POLLIN;
        if (checkWrite) pfds[i].events |= POLLOUT;
        pfds[i].revents = 0;
    }
    
    int timeout = (ms < 0) ? -1 : (int)ms;
    int result = WSAPoll(pfds, sockList->length, timeout);
    
    if (result > 0) {
        for (int i = 0; i < sockList->length; i++) {
            bool ready = false;
            if (checkRead && (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))) ready = true;
            if (checkWrite && (pfds[i].revents & POLLOUT)) ready = true;
            if (ready) {
                moon_list_append(readyList, moon_int(i));
            }
        }
    }
    
    free(pfds);
    return readyList;
}

// IOCP-based async accept for true high-concurrency servers
MoonValue* moon_tcp_accept_nonblocking(MoonValue* server) {
    SOCKET serverSock = (SOCKET)moon_to_int(server);
    
    // Use WSAPoll for non-blocking check
    WSAPOLLFD pfd;
    pfd.fd = serverSock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    if (WSAPoll(&pfd, 1, 0) <= 0) {
        return moon_null();
    }
    
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    SOCKET clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSock == INVALID_SOCKET) return moon_null();
    
    char addrBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
    
    MoonValue* result = moon_dict_new();
    moon_dict_set(result, moon_string("socket"), moon_int((int64_t)clientSock));
    moon_dict_set(result, moon_string("address"), moon_string(addrBuf));
    moon_dict_set(result, moon_string("port"), moon_int(ntohs(clientAddr.sin_port)));
    
    return result;
}

MoonValue* moon_tcp_recv_nonblocking(MoonValue* socket) {
    SOCKET sock = (SOCKET)moon_to_int(socket);
    
    // Use WSAPoll for non-blocking check
    WSAPOLLFD pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    if (WSAPoll(&pfd, 1, 0) <= 0) {
        return moon_string("");
    }
    
    char buffer[4096];
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return moon_string("");
    buffer[received] = '\0';
    
    // Create string with explicit length (supports binary data including \\0)
    char* result = moon_str_with_capacity(buffer, received, received);
    MoonStrHeader* header = moon_str_get_header(result);
    if (header) {
        header->length = received;
    }
    return moon_string_owned(result);
}

// ============================================================================
// IOCP High-Performance API (for true async I/O)
// ============================================================================

// Register socket with IOCP for async operations
MoonValue* moon_iocp_register(MoonValue* socket) {
    if (!g_iocp) return moon_bool(false);
    SOCKET sock = (SOCKET)moon_to_int(socket);
    HANDLE h = CreateIoCompletionPort((HANDLE)sock, g_iocp, (ULONG_PTR)sock, 0);
    return moon_bool(h != NULL);
}

// Wait for IOCP events (high-performance event loop)
MoonValue* moon_iocp_wait(MoonValue* timeout_ms) {
    if (!g_iocp) return moon_list_new();
    
    DWORD ms = (DWORD)moon_to_int(timeout_ms);
    OVERLAPPED_ENTRY entries[64];
    ULONG numEntries = 0;
    
    BOOL ok = GetQueuedCompletionStatusEx(g_iocp, entries, 64, &numEntries, ms, FALSE);
    
    MoonValue* results = moon_list_new();
    if (ok && numEntries > 0) {
        for (ULONG i = 0; i < numEntries; i++) {
            MoonValue* event = moon_dict_new();
            moon_dict_set(event, moon_string("socket"), moon_int((int64_t)entries[i].lpCompletionKey));
            moon_dict_set(event, moon_string("bytes"), moon_int((int64_t)entries[i].dwNumberOfBytesTransferred));
            moon_list_append(results, event);
        }
    }
    
    return results;
}

#else // Linux/POSIX

MoonValue* moon_tcp_set_nonblocking(MoonValue* socket, MoonValue* nonblocking) {
    int sock = (int)moon_to_int(socket);
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return moon_bool(false);
    
    if (moon_to_bool(nonblocking)) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    int result = fcntl(sock, F_SETFL, flags);
    return moon_bool(result != -1);
}

MoonValue* moon_tcp_has_data(MoonValue* socket) {
    int sock = (int)moon_to_int(socket);

#ifdef __APPLE__
    // macOS: use kqueue with 0 timeout for instant check
    do {
        int kq = kqueue();
        if (kq == -1) break;  // Fallback to select
        
        struct kevent ev;
        EV_SET(&ev, sock, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        
        struct timespec ts = {0, 0};  // 0 timeout = immediate return
        int nev = kevent(kq, &ev, 1, &ev, 1, &ts);
        close(kq);
        
        return moon_bool(nev > 0 && (ev.filter == EVFILT_READ));
    } while (0);
#endif

    fd_set readfds;
    struct timeval tv = {0, 0};
    
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    
    int result = select(sock + 1, &readfds, NULL, NULL, &tv);
    return moon_bool(result > 0 && FD_ISSET(sock, &readfds));
}

MoonValue* moon_tcp_select(MoonValue* sockets, MoonValue* timeout_ms, MoonValue* mode) {
    if (!moon_is_list(sockets)) return moon_list_new();
    
    MoonList* sockList = sockets->data.listVal;
    if (sockList->length == 0) return moon_list_new();
    
    const char* modeStr = moon_is_string(mode) ? mode->data.strVal : "read";
    bool checkRead = (strcmp(modeStr, "read") == 0 || strcmp(modeStr, "both") == 0);
    bool checkWrite = (strcmp(modeStr, "write") == 0 || strcmp(modeStr, "both") == 0);
    
    int64_t ms = moon_to_int(timeout_ms);
    MoonValue* readyList = moon_list_new();

#ifdef __APPLE__
    // macOS: use kqueue for high-performance I/O multiplexing
    do {
        int kq = kqueue();
        if (kq == -1) break;
        
        // Allocate events: up to 2 per socket (read + write)
        int maxEvents = sockList->length * 2;
        struct kevent* changes = (struct kevent*)malloc(maxEvents * sizeof(struct kevent));
        struct kevent* events = (struct kevent*)malloc(maxEvents * sizeof(struct kevent));
        if (!changes || !events) {
            if (changes) free(changes);
            if (events) free(events);
            close(kq);
            break;
        }
        
        int nchanges = 0;
        for (int i = 0; i < sockList->length; i++) {
            int sock = (int)moon_to_int(sockList->items[i]);
            if (checkRead) {
                EV_SET(&changes[nchanges], sock, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void*)(intptr_t)i);
                nchanges++;
            }
            if (checkWrite) {
                EV_SET(&changes[nchanges], sock, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, (void*)(intptr_t)i);
                nchanges++;
            }
        }
        
        struct timespec ts;
        struct timespec* tsp = NULL;
        if (ms >= 0) {
            ts.tv_sec = ms / 1000;
            ts.tv_nsec = (ms % 1000) * 1000000;
            tsp = &ts;
        }
        
        int nev = kevent(kq, changes, nchanges, events, maxEvents, tsp);
        
        // Track which indices we've already added (to avoid duplicates for read+write)
        bool* added = (bool*)calloc(sockList->length, sizeof(bool));
        
        for (int i = 0; i < nev; i++) {
            int idx = (int)(intptr_t)events[i].udata;
            if (idx >= 0 && idx < sockList->length && !added[idx]) {
                moon_list_append(readyList, moon_int(idx));
                added[idx] = true;
            }
        }
        
        free(added);
        free(changes);
        free(events);
        close(kq);
        return readyList;
    } while (0);
#endif

#ifdef __linux__
    // Linux: use epoll for high-performance I/O multiplexing
    do {
        int epfd = epoll_create1(0);
        if (epfd == -1) break;
        
        struct epoll_event* events = (struct epoll_event*)malloc(sockList->length * sizeof(struct epoll_event));
        if (!events) {
            close(epfd);
            break;
        }
        
        for (int i = 0; i < sockList->length; i++) {
            int sock = (int)moon_to_int(sockList->items[i]);
            struct epoll_event ev;
            ev.events = 0;
            if (checkRead) ev.events |= EPOLLIN;
            if (checkWrite) ev.events |= EPOLLOUT;
            ev.data.u32 = i;
            epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev);
        }
        
        int timeout = (ms < 0) ? -1 : (int)ms;
        int nfds = epoll_wait(epfd, events, sockList->length, timeout);
        
        for (int i = 0; i < nfds; i++) {
            moon_list_append(readyList, moon_int(events[i].data.u32));
        }
        
        free(events);
        close(epfd);
        return readyList;
    } while (0);
#endif

    // Fallback: use select()
    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    
    int maxfd = 0;
    for (int i = 0; i < sockList->length; i++) {
        int sock = (int)moon_to_int(sockList->items[i]);
        if (sock > maxfd) maxfd = sock;
        if (checkRead) FD_SET(sock, &readfds);
        if (checkWrite) FD_SET(sock, &writefds);
    }
    
    struct timeval tv;
    struct timeval* tvp = NULL;
    if (ms >= 0) {
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        tvp = &tv;
    }
    
    int result = select(maxfd + 1, checkRead ? &readfds : NULL, 
                       checkWrite ? &writefds : NULL, NULL, tvp);
    
    if (result > 0) {
        for (int i = 0; i < sockList->length; i++) {
            int sock = (int)moon_to_int(sockList->items[i]);
            bool ready = false;
            if (checkRead && FD_ISSET(sock, &readfds)) ready = true;
            if (checkWrite && FD_ISSET(sock, &writefds)) ready = true;
            if (ready) {
                moon_list_append(readyList, moon_int(i));
            }
        }
    }
    
    return readyList;
}

MoonValue* moon_tcp_accept_nonblocking(MoonValue* server) {
    int serverSock = (int)moon_to_int(server);

#ifdef __APPLE__
    // macOS: use kqueue with 0 timeout for instant check
    int kq = kqueue();
    if (kq != -1) {
        struct kevent ev;
        EV_SET(&ev, serverSock, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        
        struct timespec ts = {0, 0};  // 0 timeout = immediate return
        int nev = kevent(kq, &ev, 1, &ev, 1, &ts);
        close(kq);
        
        if (nev <= 0 || ev.filter != EVFILT_READ) {
            return moon_null();
        }
    } else {
        // Fallback to select
        fd_set readfds;
        struct timeval tv = {0, 0};
        FD_ZERO(&readfds);
        FD_SET(serverSock, &readfds);
        
        if (select(serverSock + 1, &readfds, NULL, NULL, &tv) <= 0) {
            return moon_null();
        }
    }
#else
    fd_set readfds;
    struct timeval tv = {0, 0};
    FD_ZERO(&readfds);
    FD_SET(serverSock, &readfds);
    
    if (select(serverSock + 1, &readfds, NULL, NULL, &tv) <= 0) {
        return moon_null();
    }
#endif
    
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSock == -1) return moon_null();
    
    char addrBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
    
    MoonValue* result = moon_dict_new();
    moon_dict_set(result, moon_string("socket"), moon_int((int64_t)clientSock));
    moon_dict_set(result, moon_string("address"), moon_string(addrBuf));
    moon_dict_set(result, moon_string("port"), moon_int(ntohs(clientAddr.sin_port)));
    
    return result;
}

MoonValue* moon_tcp_recv_nonblocking(MoonValue* socket) {
    int sock = (int)moon_to_int(socket);

#ifdef __APPLE__
    // macOS: use kqueue with 0 timeout for instant check
    int kq = kqueue();
    if (kq != -1) {
        struct kevent ev;
        EV_SET(&ev, sock, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, NULL);
        
        struct timespec ts = {0, 0};  // 0 timeout = immediate return
        int nev = kevent(kq, &ev, 1, &ev, 1, &ts);
        close(kq);
        
        if (nev <= 0 || ev.filter != EVFILT_READ) {
            return moon_string("");
        }
    } else {
        // Fallback to select
        fd_set readfds;
        struct timeval tv = {0, 0};
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        
        if (select(sock + 1, &readfds, NULL, NULL, &tv) <= 0) {
            return moon_string("");
        }
    }
#else
    fd_set readfds;
    struct timeval tv = {0, 0};
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    
    if (select(sock + 1, &readfds, NULL, NULL, &tv) <= 0) {
        return moon_string("");
    }
#endif
    
    char buffer[4096];
    ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return moon_string("");
    buffer[received] = '\0';
    
    // Create string with explicit length (supports binary data including \\0)
    char* result = moon_str_with_capacity(buffer, received, received);
    MoonStrHeader* header = moon_str_get_header(result);
    if (header) {
        header->length = received;
    }
    return moon_string_owned(result);
}

// IOCP stubs for Linux (IOCP is Windows-only, use epoll directly on Linux)
MoonValue* moon_iocp_register(MoonValue* socket) {
    (void)socket;
    // On Linux, use epoll directly via tcp_select which already uses epoll
    return moon_bool(true);  // Always "succeed" as Linux doesn't need explicit registration
}

MoonValue* moon_iocp_wait(MoonValue* timeout_ms) {
    (void)timeout_ms;
    // On Linux, use tcp_select with epoll instead
    return moon_list_new();
}

#endif // _WIN32 or Linux

// ============================================================================
// UDP Functions
// ============================================================================

MoonValue* moon_udp_socket(void) {
    init_wsa();
    MOON_SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return moon_int(-1);
    return moon_int((int64_t)sock);
}

MoonValue* moon_udp_bind(MoonValue* socket, MoonValue* port) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)moon_to_int(port));
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return moon_bool(false);
    }
    return moon_bool(true);
}

MoonValue* moon_udp_send(MoonValue* socket, MoonValue* host, MoonValue* port, MoonValue* data) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)moon_to_int(port));
    
    if (moon_is_string(host)) {
        struct hostent* he = gethostbyname(host->data.strVal);
        if (!he) return moon_int(-1);
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    } else {
        return moon_int(-1);
    }
    
    char* str = moon_to_string(data);
    int sent = sendto(sock, str, (int)strlen(str), 0, (struct sockaddr*)&addr, sizeof(addr));
    free(str);
    return moon_int(sent);
}

MoonValue* moon_udp_recv(MoonValue* socket) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    char buffer[4096];
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    
    int received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&addr, &addrLen);
    if (received <= 0) return moon_dict_new();
    buffer[received] = '\0';
    
    char addrBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, addrBuf, sizeof(addrBuf));
    
    MoonValue* result = moon_dict_new();
    moon_dict_set(result, moon_string("data"), moon_string(buffer));
    moon_dict_set(result, moon_string("address"), moon_string(addrBuf));
    moon_dict_set(result, moon_string("port"), moon_int(ntohs(addr.sin_port)));
    
    return result;
}

void moon_udp_close(MoonValue* socket) {
    MOON_SOCKET sock = (MOON_SOCKET)moon_to_int(socket);
    closesocket(sock);
}

#else // !MOON_HAS_NETWORK

// Stub implementations when network is disabled
MoonValue* moon_tcp_connect(MoonValue* host, MoonValue* port) { return moon_int(-1); }
MoonValue* moon_tcp_listen(MoonValue* port) { return moon_int(-1); }
MoonValue* moon_tcp_accept(MoonValue* server) { return moon_int(-1); }
MoonValue* moon_tcp_send(MoonValue* socket, MoonValue* data) { return moon_int(-1); }
MoonValue* moon_tcp_recv(MoonValue* socket) { return moon_string(""); }
void moon_tcp_close(MoonValue* socket) { }
MoonValue* moon_tcp_set_nonblocking(MoonValue* socket, MoonValue* nb) { return moon_bool(false); }
MoonValue* moon_tcp_has_data(MoonValue* socket) { return moon_bool(false); }
MoonValue* moon_tcp_select(MoonValue* sockets, MoonValue* timeout, MoonValue* mode) { return moon_list_new(); }
MoonValue* moon_tcp_accept_nonblocking(MoonValue* server) { return moon_null(); }
MoonValue* moon_tcp_recv_nonblocking(MoonValue* socket) { return moon_string(""); }
MoonValue* moon_iocp_register(MoonValue* socket) { return moon_bool(false); }
MoonValue* moon_iocp_wait(MoonValue* timeout_ms) { return moon_list_new(); }
MoonValue* moon_udp_socket(void) { return moon_int(-1); }
MoonValue* moon_udp_bind(MoonValue* socket, MoonValue* port) { return moon_bool(false); }
MoonValue* moon_udp_send(MoonValue* sock, MoonValue* host, MoonValue* port, MoonValue* data) { return moon_int(-1); }
MoonValue* moon_udp_recv(MoonValue* socket) { return moon_dict_new(); }
void moon_udp_close(MoonValue* socket) { }

#endif // MOON_HAS_NETWORK
