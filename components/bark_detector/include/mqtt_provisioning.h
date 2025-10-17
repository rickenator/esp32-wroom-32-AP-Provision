/**
 * @file mqtt_provisioning.h
 * @brief MQTT configuration storage and provisioning interface
 * 
 * Manages MQTT credentials and configuration through NVS storage
 * and captive portal provisioning interface.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT configuration stored in NVS
 */
typedef struct {
    char broker_host[128];          ///< MQTT broker hostname
    uint16_t broker_port;           ///< MQTT broker port
    char username[64];              ///< MQTT username
    char password[64];              ///< MQTT password
    char client_id[32];             ///< MQTT client ID
    char topic_prefix[64];          ///< Topic prefix for messages
    bool use_tls;                   ///< Enable TLS encryption
    char ca_cert_name[32];          ///< CA certificate name in NVS
    uint16_t keep_alive_sec;        ///< Keep-alive interval
    bool enabled;                   ///< MQTT alerts enabled
} mqtt_provision_config_t;

/**
 * @brief Initialize MQTT provisioning storage
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_init(void);

/**
 * @brief Load MQTT configuration from NVS
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not configured
 */
esp_err_t mqtt_provisioning_load(mqtt_provision_config_t* config);

/**
 * @brief Save MQTT configuration to NVS
 * @param config Configuration to save
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_save(const mqtt_provision_config_t* config);

/**
 * @brief Clear MQTT configuration from NVS
 * @return ESP_OK on success
 */
esp_err_t mqtt_provisioning_clear(void);

/**
 * @brief Check if MQTT is configured
 * @return true if configured, false otherwise
 */
bool mqtt_provisioning_is_configured(void);

/**
 * @brief Store CA certificate in NVS
 * @param cert_name Certificate name/identifier
 * @param cert_pem Certificate in PEM format
 * @param cert_len Certificate length
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_store_ca_cert(const char* cert_name, 
                                          const char* cert_pem, size_t cert_len);

/**
 * @brief Load CA certificate from NVS
 * @param cert_name Certificate name/identifier
 * @param cert_buffer Buffer to store certificate
 * @param buffer_size Size of buffer
 * @param cert_len Actual certificate length (output)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_load_ca_cert(const char* cert_name, char* cert_buffer,
                                         size_t buffer_size, size_t* cert_len);

/**
 * @brief Generate default MQTT client ID from device MAC
 * @param client_id Buffer to store client ID
 * @param size Buffer size
 */
void mqtt_provisioning_generate_client_id(char* client_id, size_t size);

/**
 * @brief Get HTML form for MQTT configuration
 * @param html_buffer Buffer to store HTML
 * @param buffer_size Buffer size
 * @param current_config Current configuration (for pre-filling form)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_get_html_form(char* html_buffer, size_t buffer_size,
                                          const mqtt_provision_config_t* current_config);

/**
 * @brief Parse MQTT configuration from HTTP POST data
 * @param post_data HTTP POST data
 * @param config Configuration structure to fill
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_provisioning_parse_post_data(const char* post_data, 
                                           mqtt_provision_config_t* config);

#ifdef __cplusplus
}
#endif