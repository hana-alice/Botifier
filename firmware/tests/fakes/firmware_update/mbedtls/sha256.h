#pragma once
#include <cstddef>
struct mbedtls_sha256_context { bool started = false; };
void mbedtls_sha256_init(mbedtls_sha256_context* context);
void mbedtls_sha256_free(mbedtls_sha256_context* context);
int mbedtls_sha256_starts(mbedtls_sha256_context* context, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context* context, const unsigned char* bytes, std::size_t size);
int mbedtls_sha256_finish(mbedtls_sha256_context* context, unsigned char* output);
