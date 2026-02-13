#ifndef MBEDTLS_MD_H
#define MBEDTLS_MD_H

#include <stddef.h>

typedef enum { MBEDTLS_MD_SHA256 } mbedtls_md_type_t;

typedef struct {
} mbedtls_md_context_t;
typedef struct {
} mbedtls_md_info_t;

inline void mbedtls_md_init(mbedtls_md_context_t *ctx) {}
inline void mbedtls_md_free(mbedtls_md_context_t *ctx) {}
inline const mbedtls_md_info_t *
mbedtls_md_info_from_type(mbedtls_md_type_t md_type) {
  return (mbedtls_md_info_t *)1;
}
inline int mbedtls_md_setup(mbedtls_md_context_t *ctx,
                            const mbedtls_md_info_t *md_info, int hmac) {
  return 0;
}
inline void mbedtls_md_hmac_starts(mbedtls_md_context_t *ctx,
                                   const unsigned char *key, size_t keylen) {}
inline void mbedtls_md_hmac_update(mbedtls_md_context_t *ctx,
                                   const unsigned char *input, size_t ilen) {}
inline void mbedtls_md_hmac_finish(mbedtls_md_context_t *ctx,
                                   unsigned char *output) {
  // Fill with dummy hash for now, or implement simple hash if needed for sig
  // verification test For now, let's just zero it out
  for (int i = 0; i < 32; i++)
    output[i] = 0;
}

#endif
