// MoonLang Runtime - TLS Module Header
// Copyright (c) 2026 greenteng.com
//
// TLS/SSL support using OpenSSL for secure connections.

#ifndef MOONRT_TLS_H
#define MOONRT_TLS_H

#include "moonrt.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TLS Context Structure
// ============================================================================

#ifdef MOON_HAS_TLS

// Forward declarations from OpenSSL (avoids including OpenSSL headers here)
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct x509_st X509;

// TLS connection context
typedef struct MoonTlsContext {
    SSL_CTX* ctx;           // OpenSSL context
    SSL* ssl;               // SSL connection
    int socket_fd;          // Underlying socket
    X509* peer_cert;        // Peer certificate (if any)
    bool is_server;         // Server or client mode
    bool connected;         // Connection established
    char* hostname;         // SNI hostname (client only)
    int verify_mode;        // Certificate verification mode
} MoonTlsContext;

// Verification modes
#define MOON_TLS_VERIFY_NONE     0
#define MOON_TLS_VERIFY_PEER     1
#define MOON_TLS_VERIFY_REQUIRED 2

#endif // MOON_HAS_TLS

// ============================================================================
// TLS Connection Functions
// ============================================================================

// Client connection
MoonValue* moon_tls_connect(MoonValue* host, MoonValue* port);

// Server functions
MoonValue* moon_tls_listen(MoonValue* port, MoonValue* cert_path, MoonValue* key_path);
MoonValue* moon_tls_accept(MoonValue* server);

// Data transfer
MoonValue* moon_tls_send(MoonValue* conn, MoonValue* data);
MoonValue* moon_tls_recv(MoonValue* conn);
MoonValue* moon_tls_recv_all(MoonValue* conn, MoonValue* max_size);

// Connection management
void moon_tls_close(MoonValue* conn);

// ============================================================================
// TLS Configuration Functions
// ============================================================================

// Certificate verification
MoonValue* moon_tls_set_verify(MoonValue* conn, MoonValue* mode);

// SNI hostname
MoonValue* moon_tls_set_hostname(MoonValue* conn, MoonValue* hostname);

// Get connection info
MoonValue* moon_tls_get_peer_cert(MoonValue* conn);
MoonValue* moon_tls_get_cipher(MoonValue* conn);
MoonValue* moon_tls_get_version(MoonValue* conn);

// ============================================================================
// Certificate Management Functions
// ============================================================================

// Load certificates
MoonValue* moon_tls_load_cert(MoonValue* path);
MoonValue* moon_tls_load_key(MoonValue* path, MoonValue* password);
MoonValue* moon_tls_load_ca(MoonValue* path);

// Certificate info
MoonValue* moon_tls_cert_info(MoonValue* cert);

// ============================================================================
// Socket Wrapper Functions
// ============================================================================

// Upgrade existing TCP socket to TLS
MoonValue* moon_tls_wrap_client(MoonValue* socket);
MoonValue* moon_tls_wrap_server(MoonValue* socket, MoonValue* cert_path, MoonValue* key_path);

// ============================================================================
// TLS Initialization/Cleanup
// ============================================================================

void moon_tls_init(void);
void moon_tls_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // MOONRT_TLS_H
