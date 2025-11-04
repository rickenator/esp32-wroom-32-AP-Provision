/**
 * @file mqtt_provisioning.cpp
 * @brief MQTT configuration storage and provisioning implementation
 */

#include "mqtt_provisioning.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "mqtt_prov";
static const char* NVS_NAMESPACE = "mqtt_cfg";
static const char* CERT_NAMESPACE = "mqtt_certs";

// NVS keys
static const char* KEY_BROKER_HOST = "broker_host";
static const char* KEY_BROKER_PORT = "broker_port";
static const char* KEY_USERNAME = "username";
static const char* KEY_PASSWORD = "password";
static const char* KEY_CLIENT_ID = "client_id";
static const char* KEY_TOPIC_PREFIX = "topic_prefix";
static const char* KEY_USE_TLS = "use_tls";
static const char* KEY_CA_CERT_NAME = "ca_cert_name";
static const char* KEY_KEEP_ALIVE = "keep_alive";
static const char* KEY_ENABLED = "enabled";

esp_err_t mqtt_provisioning_init(void) {
    // NVS is typically initialized by main application
    ESP_LOGI(TAG, "MQTT provisioning initialized");
    return ESP_OK;
}

esp_err_t mqtt_provisioning_load(mqtt_provision_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return ESP_ERR_NVS_NOT_FOUND;
    }

    // Load configuration with defaults
    size_t required_size;
    
    // Broker host
    required_size = sizeof(config->broker_host);
    err = nvs_get_str(nvs_handle, KEY_BROKER_HOST, config->broker_host, &required_size);
    if (err != ESP_OK) {
        strcpy(config->broker_host, "");
    }

    // Broker port
    err = nvs_get_u16(nvs_handle, KEY_BROKER_PORT, &config->broker_port);
    if (err != ESP_OK) {
        config->broker_port = 8883; // Default TLS port
    }

    // Username
    required_size = sizeof(config->username);
    err = nvs_get_str(nvs_handle, KEY_USERNAME, config->username, &required_size);
    if (err != ESP_OK) {
        strcpy(config->username, "");
    }

    // Password
    required_size = sizeof(config->password);
    err = nvs_get_str(nvs_handle, KEY_PASSWORD, config->password, &required_size);
    if (err != ESP_OK) {
        strcpy(config->password, "");
    }

    // Client ID
    required_size = sizeof(config->client_id);
    err = nvs_get_str(nvs_handle, KEY_CLIENT_ID, config->client_id, &required_size);
    if (err != ESP_OK) {
        mqtt_provisioning_generate_client_id(config->client_id, sizeof(config->client_id));
    }

    // Topic prefix
    required_size = sizeof(config->topic_prefix);
    err = nvs_get_str(nvs_handle, KEY_TOPIC_PREFIX, config->topic_prefix, &required_size);
    if (err != ESP_OK) {
        strcpy(config->topic_prefix, "bark_detector");
    }

    // TLS flag
    uint8_t tls_flag;
    err = nvs_get_u8(nvs_handle, KEY_USE_TLS, &tls_flag);
    config->use_tls = (err == ESP_OK) ? (tls_flag != 0) : true;

    // CA certificate name
    required_size = sizeof(config->ca_cert_name);
    err = nvs_get_str(nvs_handle, KEY_CA_CERT_NAME, config->ca_cert_name, &required_size);
    if (err != ESP_OK) {
        strcpy(config->ca_cert_name, "");
    }

    // Keep alive
    err = nvs_get_u16(nvs_handle, KEY_KEEP_ALIVE, &config->keep_alive_sec);
    if (err != ESP_OK) {
        config->keep_alive_sec = 60;
    }

    // Enabled flag
    uint8_t enabled_flag;
    err = nvs_get_u8(nvs_handle, KEY_ENABLED, &enabled_flag);
    config->enabled = (err == ESP_OK) ? (enabled_flag != 0) : false;

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Loaded MQTT config: %s:%d, enabled=%s", 
             config->broker_host, config->broker_port, 
             config->enabled ? "true" : "false");
    
    return ESP_OK;
}

esp_err_t mqtt_provisioning_save(const mqtt_provision_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace for write: %s", esp_err_to_name(err));
        return err;
    }

    // Save all configuration values
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_BROKER_HOST, config->broker_host));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, KEY_BROKER_PORT, config->broker_port));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_USERNAME, config->username));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_PASSWORD, config->password));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_CLIENT_ID, config->client_id));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_TOPIC_PREFIX, config->topic_prefix));
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, KEY_USE_TLS, config->use_tls ? 1 : 0));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, KEY_CA_CERT_NAME, config->ca_cert_name));
    ESP_ERROR_CHECK(nvs_set_u16(nvs_handle, KEY_KEEP_ALIVE, config->keep_alive_sec));
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, KEY_ENABLED, config->enabled ? 1 : 0));

    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved MQTT config: %s:%d, enabled=%s", 
                 config->broker_host, config->broker_port,
                 config->enabled ? "true" : "false");
    }

    return err;
}

esp_err_t mqtt_provisioning_clear(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs_handle);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Cleared MQTT configuration");
    return err;
}

bool mqtt_provisioning_is_configured(void) {
    mqtt_provision_config_t config;
    esp_err_t err = mqtt_provisioning_load(&config);
    return (err == ESP_OK && strlen(config.broker_host) > 0 && config.enabled);
}

esp_err_t mqtt_provisioning_store_ca_cert(const char* cert_name, 
                                          const char* cert_pem, size_t cert_len) {
    if (!cert_name || !cert_pem || cert_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open certificate NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, cert_name, cert_pem, cert_len);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stored CA certificate: %s (%d bytes)", cert_name, cert_len);
    }

    return err;
}

esp_err_t mqtt_provisioning_load_ca_cert(const char* cert_name, char* cert_buffer,
                                         size_t buffer_size, size_t* cert_len) {
    if (!cert_name || !cert_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t required_size = buffer_size;
    err = nvs_get_blob(nvs_handle, cert_name, cert_buffer, &required_size);
    
    if (err == ESP_OK && cert_len) {
        *cert_len = required_size;
    }

    nvs_close(nvs_handle);
    return err;
}

void mqtt_provisioning_generate_client_id(char* client_id, size_t size) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(client_id, size, "bark_detector_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

esp_err_t mqtt_provisioning_get_html_form(char* html_buffer, size_t buffer_size,
                                          const mqtt_provision_config_t* current_config) {
    if (!html_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Use defaults if no current config
    mqtt_provision_config_t defaults = {0};
    if (!current_config) {
        defaults.broker_port = 8883;
        defaults.use_tls = true;
        defaults.keep_alive_sec = 60;
        defaults.enabled = false;
        strcpy(defaults.topic_prefix, "bark_detector");
        mqtt_provisioning_generate_client_id(defaults.client_id, sizeof(defaults.client_id));
        current_config = &defaults;
    }

    int written = snprintf(html_buffer, buffer_size,
        "<div class=\"mqtt-config\">\n"
        "<h3>🔔 MQTT Bark Alerts</h3>\n"
        "<div class=\"form-group\">\n"
        "  <label><input type=\"checkbox\" name=\"mqtt_enabled\" %s> Enable MQTT Alerts</label>\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Broker Host:</label>\n"
        "  <input type=\"text\" name=\"mqtt_host\" value=\"%s\" placeholder=\"mqtt.example.com\" maxlength=\"127\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Port:</label>\n"
        "  <input type=\"number\" name=\"mqtt_port\" value=\"%d\" min=\"1\" max=\"65535\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Username:</label>\n"
        "  <input type=\"text\" name=\"mqtt_user\" value=\"%s\" placeholder=\"Optional\" maxlength=\"63\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Password:</label>\n"
        "  <input type=\"password\" name=\"mqtt_pass\" value=\"%s\" placeholder=\"Optional\" maxlength=\"63\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Client ID:</label>\n"
        "  <input type=\"text\" name=\"mqtt_client_id\" value=\"%s\" maxlength=\"31\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label>Topic Prefix:</label>\n"
        "  <input type=\"text\" name=\"mqtt_topic\" value=\"%s\" maxlength=\"63\">\n"
        "</div>\n"
        "<div class=\"form-group\">\n"
        "  <label><input type=\"checkbox\" name=\"mqtt_tls\" %s> Use TLS Encryption</label>\n"
        "</div>\n"
        "</div>\n",
        current_config->enabled ? "checked" : "",
        current_config->broker_host,
        current_config->broker_port,
        current_config->username,
        current_config->password,
        current_config->client_id,
        current_config->topic_prefix,
        current_config->use_tls ? "checked" : ""
    );

    return (written < buffer_size) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t mqtt_provisioning_parse_post_data(const char* post_data, 
                                           mqtt_provision_config_t* config) {
    if (!post_data || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize config with defaults
    memset(config, 0, sizeof(mqtt_provision_config_t));
    config->broker_port = 8883;
    config->use_tls = true;
    config->keep_alive_sec = 60;
    strcpy(config->topic_prefix, "bark_detector");
    mqtt_provisioning_generate_client_id(config->client_id, sizeof(config->client_id));

    // Helper function to extract parameter value
    auto extract_param = [](const char* data, const char* param, char* output, size_t max_len) {
        char search_str[64];
        snprintf(search_str, sizeof(search_str), "%s=", param);
        
        const char* start = strstr(data, search_str);
        if (!start) return false;
        
        start += strlen(search_str);
        const char* end = strchr(start, '&');
        if (!end) end = start + strlen(start);
        
        size_t len = end - start;
        if (len >= max_len) len = max_len - 1;
        
        strncpy(output, start, len);
        output[len] = '\0';
        
        // URL decode (simple version for common cases)
        char* src = output;
        char* dst = output;
        while (*src) {
            if (*src == '+') {
                *dst++ = ' ';
                src++;
            } else if (*src == '%' && src[1] && src[2]) {
                int hex_val;
                if (sscanf(src + 1, "%2x", &hex_val) == 1) {
                    *dst++ = (char)hex_val;
                    src += 3;
                } else {
                    *dst++ = *src++;
                }
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
        return true;
    };

    // Parse MQTT parameters
    char temp_buffer[128];
    
    // Check if MQTT is enabled
    config->enabled = strstr(post_data, "mqtt_enabled=on") != NULL;
    
    if (extract_param(post_data, "mqtt_host", temp_buffer, sizeof(temp_buffer))) {
        strncpy(config->broker_host, temp_buffer, sizeof(config->broker_host) - 1);
    }
    
    if (extract_param(post_data, "mqtt_port", temp_buffer, sizeof(temp_buffer))) {
        config->broker_port = (uint16_t)atoi(temp_buffer);
        if (config->broker_port == 0) config->broker_port = 8883;
    }
    
    if (extract_param(post_data, "mqtt_user", temp_buffer, sizeof(temp_buffer))) {
        strncpy(config->username, temp_buffer, sizeof(config->username) - 1);
    }
    
    if (extract_param(post_data, "mqtt_pass", temp_buffer, sizeof(temp_buffer))) {
        strncpy(config->password, temp_buffer, sizeof(config->password) - 1);
    }
    
    if (extract_param(post_data, "mqtt_client_id", temp_buffer, sizeof(temp_buffer))) {
        strncpy(config->client_id, temp_buffer, sizeof(config->client_id) - 1);
    }
    
    if (extract_param(post_data, "mqtt_topic", temp_buffer, sizeof(temp_buffer))) {
        strncpy(config->topic_prefix, temp_buffer, sizeof(config->topic_prefix) - 1);
    }
    
    // Check TLS checkbox
    config->use_tls = strstr(post_data, "mqtt_tls=on") != NULL;

    ESP_LOGI(TAG, "Parsed MQTT config: %s:%d, enabled=%s, TLS=%s", 
             config->broker_host, config->broker_port,
             config->enabled ? "true" : "false",
             config->use_tls ? "true" : "false");

    return ESP_OK;
}