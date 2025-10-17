/**
 * @file mqtt_client.h
 * @brief Secure MQTT client for ESP32-S3 TinyML bark detection alerts
 * 
 * Provides TLS-secured MQTT communication for publishing bark detection events
 * with automatic reconnection, message queuing, and certificate validation.
 * 
 * Features:
 * - TLS 1.2 encryption with CA certificate validation
 * - Automatic connection management and keep-alive
 * - Message queuing with retry logic for reliability
 * - Structured JSON payloads for bark events
 * - Real-time connection status monitoring
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT connection configuration
 */
typedef struct {
    char broker_host[128];          ///< MQTT broker hostname or IP
    uint16_t broker_port;           ///< MQTT broker port (typically 8883 for TLS)
    char username[64];              ///< MQTT username (optional)
    char password[64];              ///< MQTT password (optional)
    char client_id[32];             ///< MQTT client identifier
    char topic_prefix[64];          ///< Topic prefix for bark alerts
    bool use_tls;                   ///< Enable TLS encryption
    const char* ca_cert_pem;        ///< CA certificate in PEM format
    uint16_t keep_alive_sec;        ///< Keep-alive interval in seconds
    uint16_t timeout_ms;            ///< Connection timeout in milliseconds
} mqtt_config_t;

/**
 * @brief MQTT connection status
 */
typedef enum {
    MQTT_STATE_DISCONNECTED = 0,   ///< Not connected
    MQTT_STATE_CONNECTING,         ///< Connection in progress
    MQTT_STATE_CONNECTED,          ///< Successfully connected
    MQTT_STATE_ERROR               ///< Connection error
} mqtt_state_t;

/**
 * @brief Bark detection event for MQTT publishing
 */
typedef struct {
    uint64_t timestamp_ms;          ///< Unix timestamp in milliseconds
    uint32_t sequence_num;          ///< Event sequence number
    float confidence;               ///< Detection confidence (0.0-1.0)
    uint16_t duration_ms;           ///< Bark duration in milliseconds
    uint16_t rms_level;             ///< RMS audio level during event
    uint16_t peak_level;            ///< Peak audio level during event
    char device_id[18];             ///< Device MAC address
    char firmware_version[16];      ///< Firmware version string
} bark_event_t;

/**
 * @brief MQTT connection status callback
 * @param connected True if connected, false if disconnected
 * @param error_code ESP error code (ESP_OK if no error)
 */
typedef void (*mqtt_status_callback_t)(bool connected, esp_err_t error_code);

/**
 * @brief Initialize MQTT client with configuration
 * @param config MQTT configuration structure
 * @param status_callback Optional callback for connection status updates
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_init(const mqtt_config_t* config, mqtt_status_callback_t status_callback);

/**
 * @brief Start MQTT client and connect to broker
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Stop MQTT client and disconnect from broker
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief Get current MQTT connection state
 * @return Current connection state
 */
mqtt_state_t mqtt_client_get_state(void);

/**
 * @brief Publish bark detection event to MQTT broker
 * @param event Bark detection event data
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_publish_bark_event(const bark_event_t* event);

/**
 * @brief Publish generic message to MQTT topic
 * @param topic MQTT topic (relative to configured prefix)
 * @param message Message payload
 * @param message_len Message length in bytes
 * @param qos Quality of Service level (0, 1, or 2)
 * @param retain Retain flag for message
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_publish_message(const char* topic, const char* message, 
                              size_t message_len, int qos, bool retain);

/**
 * @brief Update MQTT configuration (requires restart)
 * @param config New MQTT configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_update_config(const mqtt_config_t* config);

/**
 * @brief Get MQTT client statistics
 * @param connected_time_ms Time connected in milliseconds
 * @param messages_sent Total messages sent
 * @param messages_failed Total messages failed
 * @param last_error Last error code
 * @return ESP_OK on success
 */
esp_err_t mqtt_client_get_stats(uint64_t* connected_time_ms, uint32_t* messages_sent,
                               uint32_t* messages_failed, esp_err_t* last_error);

/**
 * @brief Test MQTT connection with current configuration
 * @param timeout_ms Maximum time to wait for connection test
 * @return ESP_OK if connection successful, error code otherwise
 */
esp_err_t mqtt_client_test_connection(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif