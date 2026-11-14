#pragma once

// Host-test configuration for mbedtls 3.6.5. AES-256 and SHA-256 only.
// The ESP32 builds use the framework configuration instead of this file.
// Windows (this toolchain) requires the platform module; mbedtls turns on
// its snprintf replacements once that module is present.

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_SHA256_C
#define MBEDTLS_PLATFORM_C
