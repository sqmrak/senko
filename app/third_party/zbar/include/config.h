/* QR-only ZBar configuration for the Senko iOS capture path. */
#ifndef SENKO_ZBAR_CONFIG_H
#define SENKO_ZBAR_CONFIG_H

#define ENABLE_CODABAR 0
#define ENABLE_CODE128 0
#define ENABLE_CODE39 0
#define ENABLE_CODE93 0
#define ENABLE_DATABAR 0
#define ENABLE_EAN 0
#define ENABLE_I25 0
#define ENABLE_NLS 0
#define ENABLE_PDF417 0
#define ENABLE_QRCODE 1
#define ENABLE_SQCODE 0

#define HAVE_ERRNO_H 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1

#define ZBAR_VERSION_MAJOR 0
#define ZBAR_VERSION_MINOR 23
#define ZBAR_VERSION_PATCH 93

#endif
