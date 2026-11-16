/* Minimal Mbed TLS configuration for BDS-RE: login JWT verification (ECDSA
 * P-384 + SHA-384) and the session key exchange (ECDH P-384, SHA-256, AES).
 * No TLS, X.509 or PSA modules are built. */
#ifndef BDS_MBEDTLS_CONFIG_H
#define BDS_MBEDTLS_CONFIG_H

#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD_C

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CFB
#define MBEDTLS_CIPHER_MODE_CTR

#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#endif
