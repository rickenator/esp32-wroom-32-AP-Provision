/**
 * @file ca_certificates.h
 * @brief Common CA certificates for MQTT TLS connections
 * 
 * Contains trusted root CA certificates for popular MQTT brokers
 * like AWS IoT, Google Cloud IoT, Azure IoT Hub, and others.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Let's Encrypt Root CA (used by many MQTT brokers)
extern const char* LETS_ENCRYPT_ROOT_CA;

// Amazon Root CA 1 (for AWS IoT Core)
extern const char* AMAZON_ROOT_CA_1;

// DigiCert Global Root CA (used by many cloud services)
extern const char* DIGICERT_GLOBAL_ROOT_CA;

// Cloudflare Root CA (for Cloudflare services)
extern const char* CLOUDFLARE_ROOT_CA;

// Get CA certificate by name
const char* get_ca_certificate(const char* broker_hostname);

#ifdef __cplusplus
}
#endif