// MoonLang Runtime - TLS Module
// Copyright (c) 2026 greenteng.com
//
// TLS/SSL support using OpenSSL for secure connections.
// Provides both client and server TLS functionality.

#include "moonrt_core.h"
#include "moonrt_tls.h"

#ifdef MOON_HAS_TLS

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>  // For Windows certificate store
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")  // For Windows certificate store
#define MOON_SOCKET SOCKET
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define closesocket_impl closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define MOON_SOCKET int
#define INVALID_SOCKET_VAL -1
#define closesocket_impl close
#endif

#include <string.h>
#include <stdlib.h>

// ============================================================================
// Global State
// ============================================================================

static bool g_tls_initialized = false;

// ============================================================================
// TLS Initialization/Cleanup
// ============================================================================

void moon_tls_init(void) {
    if (g_tls_initialized) return;
    
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    g_tls_initialized = true;
}

void moon_tls_cleanup(void) {
    if (!g_tls_initialized) return;
    
    // Cleanup OpenSSL
    EVP_cleanup();
    ERR_free_strings();
    
    g_tls_initialized = false;
}

// ============================================================================
// Helper Functions
// ============================================================================

static MoonTlsContext* create_tls_context(void) {
    MoonTlsContext* ctx = (MoonTlsContext*)malloc(sizeof(MoonTlsContext));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(MoonTlsContext));
    ctx->socket_fd = -1;
    ctx->verify_mode = MOON_TLS_VERIFY_PEER;
    
    return ctx;
}

static void free_tls_context(MoonTlsContext* ctx) {
    if (!ctx) return;
    
    if (ctx->ssl) {
        SSL_shutdown(ctx->ssl);
        SSL_free(ctx->ssl);
    }
    if (ctx->ctx) {
        SSL_CTX_free(ctx->ctx);
    }
    if (ctx->peer_cert) {
        X509_free(ctx->peer_cert);
    }
    if (ctx->hostname) {
        free(ctx->hostname);
    }
    if (ctx->socket_fd >= 0) {
        closesocket_impl(ctx->socket_fd);
    }
    
    free(ctx);
}

static const char* get_ssl_error_string(void) {
    unsigned long err = ERR_get_error();
    if (err == 0) return "No error";
    return ERR_error_string(err, NULL);
}

// ============================================================================
// TLS Client Connection
// ============================================================================

MoonValue* moon_tls_connect(MoonValue* host, MoonValue* port) {
    moon_tls_init();
    
    if (!moon_is_string(host)) {
        return moon_null();
    }
    
    const char* hostname = host->data.strVal;
    int portnum = (int)moon_to_int(port);
    
    // Create socket
    MOON_SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VAL) {
        return moon_null();
    }
    
    // Resolve hostname
    struct hostent* he = gethostbyname(hostname);
    if (!he) {
        closesocket_impl(sock);
        return moon_null();
    }
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)portnum);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket_impl(sock);
        return moon_null();
    }
    
    // Create TLS context
    MoonTlsContext* ctx = create_tls_context();
    if (!ctx) {
        closesocket_impl(sock);
        return moon_null();
    }
    
    ctx->socket_fd = (int)sock;
    ctx->is_server = false;
    ctx->hostname = strdup(hostname);
    
    // Create SSL context with TLS client method
    ctx->ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx->ctx) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Load Windows certificate store for verification
#ifdef _WIN32
    // Try to load from Windows certificate store
    X509_STORE* store = SSL_CTX_get_cert_store(ctx->ctx);
    if (store) {
        HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
        if (hStore) {
            PCCERT_CONTEXT pContext = NULL;
            while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
                X509* x509 = d2i_X509(NULL, (const unsigned char**)&pContext->pbCertEncoded, 
                                       pContext->cbCertEncoded);
                if (x509) {
                    X509_STORE_add_cert(store, x509);
                    X509_free(x509);
                }
            }
            CertCloseStore(hStore, 0);
        }
    }
#endif
    
    // Set reasonable defaults - verify peer certificate
    SSL_CTX_set_verify(ctx->ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(ctx->ctx);
    
    // Create SSL connection
    ctx->ssl = SSL_new(ctx->ctx);
    if (!ctx->ssl) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Set SNI hostname
    SSL_set_tlsext_host_name(ctx->ssl, hostname);
    
    // Enable hostname verification
    SSL_set1_host(ctx->ssl, hostname);
    
    // Attach socket
    SSL_set_fd(ctx->ssl, (int)sock);
    
    // Perform TLS handshake
    int ret = SSL_connect(ctx->ssl);
    if (ret != 1) {
        int err = SSL_get_error(ctx->ssl, ret);
        // For debugging: fprintf(stderr, "TLS handshake failed: %d - %s\n", err, get_ssl_error_string());
        free_tls_context(ctx);
        return moon_null();
    }
    
    ctx->connected = true;
    
    // Get peer certificate
    ctx->peer_cert = SSL_get_peer_certificate(ctx->ssl);
    
    // Return as integer (pointer value)
    return moon_int((int64_t)(uintptr_t)ctx);
}

// ============================================================================
// TLS Server Functions
// ============================================================================

MoonValue* moon_tls_listen(MoonValue* port, MoonValue* cert_path, MoonValue* key_path) {
    moon_tls_init();
    
    if (!moon_is_string(cert_path) || !moon_is_string(key_path)) {
        return moon_null();
    }
    
    int portnum = (int)moon_to_int(port);
    const char* cert_file = cert_path->data.strVal;
    const char* key_file = key_path->data.strVal;
    
    // Create TLS context
    MoonTlsContext* ctx = create_tls_context();
    if (!ctx) return moon_null();
    
    ctx->is_server = true;
    
    // Create SSL context with TLS server method
    ctx->ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx->ctx) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Load certificate
    if (SSL_CTX_use_certificate_file(ctx->ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx->ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Verify private key
    if (SSL_CTX_check_private_key(ctx->ctx) != 1) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    // Create listening socket
    MOON_SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VAL) {
        free_tls_context(ctx);
        return moon_null();
    }
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)portnum);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket_impl(sock);
        free_tls_context(ctx);
        return moon_null();
    }
    
    if (listen(sock, 10) != 0) {
        closesocket_impl(sock);
        free_tls_context(ctx);
        return moon_null();
    }
    
    ctx->socket_fd = (int)sock;
    ctx->connected = true;
    
    return moon_int((int64_t)(uintptr_t)ctx);
}

MoonValue* moon_tls_accept(MoonValue* server) {
    MoonTlsContext* server_ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(server);
    if (!server_ctx || !server_ctx->is_server || !server_ctx->ctx) {
        return moon_null();
    }
    
    // Accept TCP connection
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    MOON_SOCKET client_sock = accept(server_ctx->socket_fd, 
                                     (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock == INVALID_SOCKET_VAL) {
        return moon_null();
    }
    
    // Create client TLS context
    MoonTlsContext* client_ctx = create_tls_context();
    if (!client_ctx) {
        closesocket_impl(client_sock);
        return moon_null();
    }
    
    client_ctx->socket_fd = (int)client_sock;
    client_ctx->is_server = false;
    
    // Create SSL connection (use server's SSL_CTX)
    client_ctx->ssl = SSL_new(server_ctx->ctx);
    if (!client_ctx->ssl) {
        free_tls_context(client_ctx);
        return moon_null();
    }
    
    SSL_set_fd(client_ctx->ssl, (int)client_sock);
    
    // Perform TLS handshake
    int ret = SSL_accept(client_ctx->ssl);
    if (ret != 1) {
        free_tls_context(client_ctx);
        return moon_null();
    }
    
    client_ctx->connected = true;
    
    // Return connection info as dictionary (use thread-safe inet_ntop)
    char addrBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addrBuf, sizeof(addrBuf));
    
    MoonValue* result = moon_dict_new();
    MoonValue* connKey = moon_string("conn");
    MoonValue* connVal = moon_int((int64_t)(uintptr_t)client_ctx);
    MoonValue* addrKey = moon_string("address");
    MoonValue* addrVal = moon_string(addrBuf);
    MoonValue* portKey = moon_string("port");
    MoonValue* portVal = moon_int(ntohs(client_addr.sin_port));
    
    moon_dict_set(result, connKey, connVal);
    moon_dict_set(result, addrKey, addrVal);
    moon_dict_set(result, portKey, portVal);
    
    moon_release(connKey);
    moon_release(connVal);
    moon_release(addrKey);
    moon_release(addrVal);
    moon_release(portKey);
    moon_release(portVal);
    
    return result;
}

// ============================================================================
// Data Transfer
// ============================================================================

MoonValue* moon_tls_send(MoonValue* conn, MoonValue* data) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl || !ctx->connected) {
        return moon_int(-1);
    }
    
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
    
    int sent = SSL_write(ctx->ssl, str, len);
    
    if (toFree) {
        free(toFree);
    }
    
    if (sent <= 0) {
        return moon_int(-1);
    }
    
    return moon_int(sent);
}

MoonValue* moon_tls_recv(MoonValue* conn) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl || !ctx->connected) {
        return moon_string("");
    }
    
    char buffer[8192];
    int received = SSL_read(ctx->ssl, buffer, sizeof(buffer) - 1);
    
    if (received <= 0) {
        int err = SSL_get_error(ctx->ssl, received);
        if (err == SSL_ERROR_ZERO_RETURN) {
            // Connection closed cleanly
            ctx->connected = false;
        }
        return moon_string("");
    }
    
    buffer[received] = '\0';
    return moon_string(buffer);
}

MoonValue* moon_tls_recv_all(MoonValue* conn, MoonValue* max_size) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl || !ctx->connected) {
        return moon_string("");
    }
    
    int max_bytes = (int)moon_to_int(max_size);
    if (max_bytes <= 0) max_bytes = 1024 * 1024; // 1MB default
    
    char* buffer = (char*)malloc(max_bytes + 1);
    if (!buffer) return moon_string("");
    
    int total = 0;
    while (total < max_bytes) {
        int received = SSL_read(ctx->ssl, buffer + total, max_bytes - total);
        if (received <= 0) break;
        total += received;
        
        // Check if more data available
        int pending = SSL_pending(ctx->ssl);
        if (pending <= 0) break;
    }
    
    buffer[total] = '\0';
    MoonValue* result = moon_string(buffer);
    free(buffer);
    
    return result;
}

// ============================================================================
// Connection Management
// ============================================================================

void moon_tls_close(MoonValue* conn) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (ctx) {
        free_tls_context(ctx);
    }
}

// ============================================================================
// TLS Configuration
// ============================================================================

MoonValue* moon_tls_set_verify(MoonValue* conn, MoonValue* mode) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ctx) {
        return moon_bool(false);
    }
    
    const char* mode_str = moon_is_string(mode) ? mode->data.strVal : "";
    
    int verify_mode = SSL_VERIFY_NONE;
    if (strcmp(mode_str, "peer") == 0 || strcmp(mode_str, "required") == 0) {
        verify_mode = SSL_VERIFY_PEER;
        if (strcmp(mode_str, "required") == 0) {
            verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
    }
    
    SSL_CTX_set_verify(ctx->ctx, verify_mode, NULL);
    ctx->verify_mode = (strcmp(mode_str, "required") == 0) ? MOON_TLS_VERIFY_REQUIRED :
                       (strcmp(mode_str, "peer") == 0) ? MOON_TLS_VERIFY_PEER : 
                       MOON_TLS_VERIFY_NONE;
    
    return moon_bool(true);
}

MoonValue* moon_tls_set_hostname(MoonValue* conn, MoonValue* hostname) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl) {
        return moon_bool(false);
    }
    
    if (!moon_is_string(hostname)) {
        return moon_bool(false);
    }
    
    const char* name = hostname->data.strVal;
    
    // Set SNI
    SSL_set_tlsext_host_name(ctx->ssl, name);
    
    // Update stored hostname
    if (ctx->hostname) free(ctx->hostname);
    ctx->hostname = strdup(name);
    
    return moon_bool(true);
}

// ============================================================================
// Connection Info
// ============================================================================

MoonValue* moon_tls_get_peer_cert(MoonValue* conn) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl) {
        return moon_null();
    }
    
    X509* cert = SSL_get_peer_certificate(ctx->ssl);
    if (!cert) {
        return moon_null();
    }
    
    MoonValue* result = moon_dict_new();
    
    // Subject
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
    moon_dict_set(result, moon_string("subject"), moon_string(subject));
    
    // Issuer
    char issuer[256];
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer, sizeof(issuer));
    moon_dict_set(result, moon_string("issuer"), moon_string(issuer));
    
    // Serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, NULL);
    char* serial_str = BN_bn2hex(bn);
    moon_dict_set(result, moon_string("serial"), moon_string(serial_str ? serial_str : ""));
    if (serial_str) OPENSSL_free(serial_str);
    if (bn) BN_free(bn);
    
    // Validity
    const ASN1_TIME* not_before = X509_get0_notBefore(cert);
    const ASN1_TIME* not_after = X509_get0_notAfter(cert);
    
    BIO* bio = BIO_new(BIO_s_mem());
    char buf[64];
    
    ASN1_TIME_print(bio, not_before);
    int len = BIO_read(bio, buf, sizeof(buf) - 1);
    buf[len] = '\0';
    moon_dict_set(result, moon_string("not_before"), moon_string(buf));
    
    BIO_reset(bio);
    ASN1_TIME_print(bio, not_after);
    len = BIO_read(bio, buf, sizeof(buf) - 1);
    buf[len] = '\0';
    moon_dict_set(result, moon_string("not_after"), moon_string(buf));
    
    BIO_free(bio);
    X509_free(cert);
    
    return result;
}

MoonValue* moon_tls_get_cipher(MoonValue* conn) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl) {
        return moon_string("");
    }
    
    const char* cipher = SSL_get_cipher(ctx->ssl);
    return moon_string(cipher ? cipher : "");
}

MoonValue* moon_tls_get_version(MoonValue* conn) {
    MoonTlsContext* ctx = (MoonTlsContext*)(uintptr_t)moon_to_int(conn);
    if (!ctx || !ctx->ssl) {
        return moon_string("");
    }
    
    const char* version = SSL_get_version(ctx->ssl);
    return moon_string(version ? version : "");
}

// ============================================================================
// Certificate Management
// ============================================================================

MoonValue* moon_tls_load_cert(MoonValue* path) {
    if (!moon_is_string(path)) {
        return moon_null();
    }
    
    FILE* fp = fopen(path->data.strVal, "r");
    if (!fp) {
        return moon_null();
    }
    
    X509* cert = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose(fp);
    
    if (!cert) {
        return moon_null();
    }
    
    return moon_int((int64_t)(uintptr_t)cert);
}

MoonValue* moon_tls_load_key(MoonValue* path, MoonValue* password) {
    if (!moon_is_string(path)) {
        return moon_null();
    }
    
    const char* pwd = moon_is_string(password) ? password->data.strVal : NULL;
    
    FILE* fp = fopen(path->data.strVal, "r");
    if (!fp) {
        return moon_null();
    }
    
    EVP_PKEY* key = PEM_read_PrivateKey(fp, NULL, NULL, (void*)pwd);
    fclose(fp);
    
    if (!key) {
        return moon_null();
    }
    
    return moon_int((int64_t)(uintptr_t)key);
}

MoonValue* moon_tls_load_ca(MoonValue* path) {
    moon_tls_init();
    
    if (!moon_is_string(path)) {
        return moon_bool(false);
    }
    
    // This would need to be applied to a specific SSL_CTX
    // For now, just verify the file exists and is readable
    FILE* fp = fopen(path->data.strVal, "r");
    if (!fp) {
        return moon_bool(false);
    }
    fclose(fp);
    
    return moon_bool(true);
}

MoonValue* moon_tls_cert_info(MoonValue* cert) {
    X509* x509 = (X509*)(uintptr_t)moon_to_int(cert);
    if (!x509) {
        return moon_null();
    }
    
    MoonValue* result = moon_dict_new();
    
    // Subject
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(x509), subject, sizeof(subject));
    moon_dict_set(result, moon_string("subject"), moon_string(subject));
    
    // Issuer  
    char issuer[256];
    X509_NAME_oneline(X509_get_issuer_name(x509), issuer, sizeof(issuer));
    moon_dict_set(result, moon_string("issuer"), moon_string(issuer));
    
    return result;
}

// ============================================================================
// Socket Wrapper Functions
// ============================================================================

MoonValue* moon_tls_wrap_client(MoonValue* socket) {
    moon_tls_init();
    
    int sock = (int)moon_to_int(socket);
    if (sock < 0) {
        return moon_null();
    }
    
    MoonTlsContext* ctx = create_tls_context();
    if (!ctx) {
        return moon_null();
    }
    
    ctx->socket_fd = sock;
    ctx->is_server = false;
    
    ctx->ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx->ctx) {
        free(ctx);  // Don't close socket - it was passed in
        return moon_null();
    }
    
    SSL_CTX_set_default_verify_paths(ctx->ctx);
    
    ctx->ssl = SSL_new(ctx->ctx);
    if (!ctx->ssl) {
        SSL_CTX_free(ctx->ctx);
        free(ctx);
        return moon_null();
    }
    
    SSL_set_fd(ctx->ssl, sock);
    
    if (SSL_connect(ctx->ssl) != 1) {
        SSL_free(ctx->ssl);
        SSL_CTX_free(ctx->ctx);
        free(ctx);
        return moon_null();
    }
    
    ctx->connected = true;
    
    return moon_int((int64_t)(uintptr_t)ctx);
}

MoonValue* moon_tls_wrap_server(MoonValue* socket, MoonValue* cert_path, MoonValue* key_path) {
    moon_tls_init();
    
    if (!moon_is_string(cert_path) || !moon_is_string(key_path)) {
        return moon_null();
    }
    
    int sock = (int)moon_to_int(socket);
    if (sock < 0) {
        return moon_null();
    }
    
    MoonTlsContext* ctx = create_tls_context();
    if (!ctx) {
        return moon_null();
    }
    
    ctx->socket_fd = sock;
    ctx->is_server = false;  // This is a wrapped client connection
    
    ctx->ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx->ctx) {
        free(ctx);
        return moon_null();
    }
    
    if (SSL_CTX_use_certificate_file(ctx->ctx, cert_path->data.strVal, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx->ctx, key_path->data.strVal, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx->ctx) != 1) {
        SSL_CTX_free(ctx->ctx);
        free(ctx);
        return moon_null();
    }
    
    ctx->ssl = SSL_new(ctx->ctx);
    if (!ctx->ssl) {
        SSL_CTX_free(ctx->ctx);
        free(ctx);
        return moon_null();
    }
    
    SSL_set_fd(ctx->ssl, sock);
    
    if (SSL_accept(ctx->ssl) != 1) {
        SSL_free(ctx->ssl);
        SSL_CTX_free(ctx->ctx);
        free(ctx);
        return moon_null();
    }
    
    ctx->connected = true;
    
    return moon_int((int64_t)(uintptr_t)ctx);
}

#else // !MOON_HAS_TLS

// ============================================================================
// Stub Implementations (when TLS is disabled)
// ============================================================================

void moon_tls_init(void) {}
void moon_tls_cleanup(void) {}

MoonValue* moon_tls_connect(MoonValue* host, MoonValue* port) {
    (void)host; (void)port;
    return moon_null();
}

MoonValue* moon_tls_listen(MoonValue* port, MoonValue* cert, MoonValue* key) {
    (void)port; (void)cert; (void)key;
    return moon_null();
}

MoonValue* moon_tls_accept(MoonValue* server) {
    (void)server;
    return moon_null();
}

MoonValue* moon_tls_send(MoonValue* conn, MoonValue* data) {
    (void)conn; (void)data;
    return moon_int(-1);
}

MoonValue* moon_tls_recv(MoonValue* conn) {
    (void)conn;
    return moon_string("");
}

MoonValue* moon_tls_recv_all(MoonValue* conn, MoonValue* max) {
    (void)conn; (void)max;
    return moon_string("");
}

void moon_tls_close(MoonValue* conn) {
    (void)conn;
}

MoonValue* moon_tls_set_verify(MoonValue* conn, MoonValue* mode) {
    (void)conn; (void)mode;
    return moon_bool(false);
}

MoonValue* moon_tls_set_hostname(MoonValue* conn, MoonValue* hostname) {
    (void)conn; (void)hostname;
    return moon_bool(false);
}

MoonValue* moon_tls_get_peer_cert(MoonValue* conn) {
    (void)conn;
    return moon_null();
}

MoonValue* moon_tls_get_cipher(MoonValue* conn) {
    (void)conn;
    return moon_string("");
}

MoonValue* moon_tls_get_version(MoonValue* conn) {
    (void)conn;
    return moon_string("");
}

MoonValue* moon_tls_load_cert(MoonValue* path) {
    (void)path;
    return moon_null();
}

MoonValue* moon_tls_load_key(MoonValue* path, MoonValue* password) {
    (void)path; (void)password;
    return moon_null();
}

MoonValue* moon_tls_load_ca(MoonValue* path) {
    (void)path;
    return moon_bool(false);
}

MoonValue* moon_tls_cert_info(MoonValue* cert) {
    (void)cert;
    return moon_null();
}

MoonValue* moon_tls_wrap_client(MoonValue* socket) {
    (void)socket;
    return moon_null();
}

MoonValue* moon_tls_wrap_server(MoonValue* socket, MoonValue* cert, MoonValue* key) {
    (void)socket; (void)cert; (void)key;
    return moon_null();
}

#endif // MOON_HAS_TLS
