#ifndef _OPENSSL_SSL_H
#define _OPENSSL_SSL_H

#include <stddef.h>

typedef void SSL;
typedef void SSL_CTX;
typedef void SSL_METHOD;
typedef void X509;
typedef void X509_STORE;
typedef void BIO;
typedef void EVP_PKEY;
typedef void SSL_SESSION;
typedef long SSL_CTX_set_options;

#define SSL_VERIFY_NONE 0
#define SSL_VERIFY_PEER 1
#define SSL_ERROR_NONE 0
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_SYSCALL 5
#define SSL_ERROR_SSL 1
#define SSL_ERROR_ZERO_RETURN 6
#define SSL_CTRL_OPTIONS 32
#define SSL_OP_ALL 0x000FFFFFL
#define SSL_OP_NO_SSLv2 0x01000000L
#define SSL_OP_NO_SSLv3 0x02000000L
#define SSL_OP_NO_TLSv1 0x04000000L
#define SSL_FILETYPE_PEM 1
#define SSL_VERIFY_NONE 0

SSL_CTX *SSL_CTX_new(const SSL_METHOD *method);
void SSL_CTX_free(SSL_CTX *ctx);
const SSL_METHOD *SSLv23_client_method(void);
const SSL_METHOD *TLS_client_method(void);
long SSL_CTX_ctrl(SSL_CTX *ctx, int cmd, long larg, void *parg);
SSL *SSL_new(SSL_CTX *ctx);
void SSL_free(SSL *ssl);
int SSL_set_fd(SSL *ssl, int fd);
int SSL_connect(SSL *ssl);
int SSL_write(SSL *ssl, const void *buf, int num);
int SSL_read(SSL *ssl, void *buf, int num);
int SSL_get_error(const SSL *ssl, int ret);
int SSL_shutdown(SSL *ssl);
int SSL_CTX_use_certificate_file(SSL_CTX *ctx, const char *file, int type);
int SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx, const char *file, int type);
X509 *SSL_get_peer_certificate(const SSL *ssl);
void X509_free(X509 *x509);

#endif
