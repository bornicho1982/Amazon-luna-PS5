/*
 * Overlay de configuracion de mbedTLS para Luna PS5.
 *
 * libdatachannel 0.23.1 usa la API DTLS-SRTP de mbedTLS
 * (mbedtls_ssl_get_dtls_srtp_negotiation_result). El mbedtls_config.h
 * vendorizado la deja deshabilitada, asi que la activamos aqui sin modificar
 * el arbol de terceros (tools/reference esta ignorado por git).
 *
 * Este header se aplica con MBEDTLS_USER_CONFIG_FILE tanto a mbedTLS como a
 * libdatachannel para que ambos vean la misma configuracion.
 */
#ifndef LUNA_MBEDTLS_USER_CONFIG_H
#define LUNA_MBEDTLS_USER_CONFIG_H

#define MBEDTLS_SSL_DTLS_SRTP

#if !defined(_WIN32)
/* The app makes HTTPS requests from several threads at once (lists, prices,
 * images) while WebRTC runs DTLS: the shared random generator and the PSA
 * crypto state (TLS 1.3) need Mbed TLS's own locking, or handshakes fail
 * with "This is a bug in the library" (-0x006E, CORRUPTION_DETECTED). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
#endif

#endif /* LUNA_MBEDTLS_USER_CONFIG_H */
