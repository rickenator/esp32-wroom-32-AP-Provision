/**
 * @file mqtt_client.cpp
 * @brief Secure MQTT client implementation for ESP32-S3 bark detection alerts
 * 
 * Implementation of TLS-secured MQTT client using ESP-IDF MQTT library.
 * Provides reliable message delivery with automatic reconnection and queuing.
 */

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "mqtt_client.h" // ESP-IDF MQTT client
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char* TAG = "mqtt_bark";

// MQTT client state
static esp_mqtt_client_handle_t mqtt_client_handle = NULL;
static mqtt_config_t current_config = {0};
static mqtt_state_t current_state = MQTT_STATE_DISCONNECTED;
static mqtt_status_callback_t status_callback = NULL;

// Statistics tracking
static uint64_t connect_time_ms = 0;
static uint32_t messages_sent = 0;
static uint32_t messages_failed = 0;
static esp_err_t last_error = ESP_OK;

// Message queue for reliable delivery
#define MQTT_QUEUE_SIZE 16
static QueueHandle_t mqtt_message_queue = NULL;

typedef struct {
    char topic[128];
    char payload[512];
    int qos;
    bool retain;
} queued_message_t;

// Forward declarations
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                              int32_t event_id, void* event_data);
static void mqtt_queue_task(void* pvParameters);
static esp_err_t create_bark_event_json(const bark_event_t* event, char* json_buffer, size_t buffer_size);
static void get_device_mac_string(char* mac_str, size_t size);

esp_err_t mqtt_client_init(const mqtt_config_t* config, mqtt_status_callback_t callback) {
    if (!config) {
        ESP_LOGE(TAG, "MQTT config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    memcpy(&current_config, config, sizeof(mqtt_config_t));
    status_callback = callback;

    // Create message queue for reliable delivery
    if (!mqtt_message_queue) {
        mqtt_message_queue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(queued_message_t));
        if (!mqtt_message_queue) {
            ESP_LOGE(TAG, "Failed to create MQTT message queue");
            return ESP_ERR_NO_MEM;
        }
    }

    // Create queue processing task
    xTaskCreatePinnedToCore(mqtt_queue_task, "mqtt_queue", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "MQTT client initialized for broker: %s:%d", 
             config->broker_host, config->broker_port);
    
    return ESP_OK;
}

esp_err_t mqtt_client_start(void) {
    if (mqtt_client_handle) {
        ESP_LOGW(TAG, "MQTT client already started");
        return ESP_OK;
    }

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.hostname = current_config.broker_host;
    mqtt_cfg.broker.address.port = current_config.broker_port;
    mqtt_cfg.credentials.username = current_config.username[0] ? current_config.username : NULL;
    mqtt_cfg.credentials.authentication.password = current_config.password[0] ? current_config.password : NULL;
    mqtt_cfg.credentials.client_id = current_config.client_id;
    mqtt_cfg.session.keepalive = current_config.keep_alive_sec;
    mqtt_cfg.network.timeout_ms = current_config.timeout_ms;

    // Configure TLS if enabled
    if (current_config.use_tls) {
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        if (current_config.ca_cert_pem) {
            mqtt_cfg.broker.verification.certificate = current_config.ca_cert_pem;
        }
        mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
    } else {
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    }

    // Initialize MQTT client
    mqtt_client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client_handle) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register event handler
    esp_mqtt_client_register_event(mqtt_client_handle, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, 
                                  mqtt_event_handler, NULL);

    // Start client
    esp_err_t err = esp_mqtt_client_start(mqtt_client_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client_handle);
        mqtt_client_handle = NULL;
        return err;
    }

    current_state = MQTT_STATE_CONNECTING;
    ESP_LOGI(TAG, "MQTT client started, connecting to %s:%d", 
             current_config.broker_host, current_config.broker_port);
    
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void) {
    if (!mqtt_client_handle) {
        return ESP_OK;
    }

    esp_err_t err = esp_mqtt_client_stop(mqtt_client_handle);
    if (err == ESP_OK) {
        esp_mqtt_client_destroy(mqtt_client_handle);
        mqtt_client_handle = NULL;
        current_state = MQTT_STATE_DISCONNECTED;
        ESP_LOGI(TAG, "MQTT client stopped");
    }
    
    return err;
}

mqtt_state_t mqtt_client_get_state(void) {
    return current_state;
}

esp_err_t mqtt_publish_bark_event(const bark_event_t* event) {
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create JSON payload
    char json_buffer[512];
    esp_err_t err = create_bark_event_json(event, json_buffer, sizeof(json_buffer));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create bark event JSON");
        return err;
    }

    // Create full topic path
    char full_topic[192];
    snprintf(full_topic, sizeof(full_topic), "%s/bark/detected", current_config.topic_prefix);

    // Publish message
    return mqtt_publish_message(full_topic, json_buffer, strlen(json_buffer), 1, false);
}

esp_err_t mqtt_publish_message(const char* topic, const char* message, 
                              size_t message_len, int qos, bool retain) {
    if (!topic || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    // If client is connected, publish immediately
    if (current_state == MQTT_STATE_CONNECTED && mqtt_client_handle) {
        int msg_id = esp_mqtt_client_publish(mqtt_client_handle, topic, message, 
                                           message_len, qos, retain ? 1 : 0);
        if (msg_id >= 0) {
            messages_sent++;
            ESP_LOGI(TAG, "Published to %s (msg_id=%d)", topic, msg_id);
            return ESP_OK;
        } else {
            messages_failed++;
            ESP_LOGE(TAG, "Failed to publish to %s", topic);
            last_error = ESP_FAIL;
        }
    }

    // Queue message for later delivery
    queued_message_t queued_msg = {0};
    strncpy(queued_msg.topic, topic, sizeof(queued_msg.topic) - 1);
    strncpy(queued_msg.payload, message, sizeof(queued_msg.payload) - 1);
    queued_msg.qos = qos;
    queued_msg.retain = retain;

    if (xQueueSend(mqtt_message_queue, &queued_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGI(TAG, "Queued message for topic: %s", topic);
        return ESP_OK;
    } else {
        messages_failed++;
        ESP_LOGE(TAG, "Failed to queue message, queue full");
        return ESP_ERR_NO_MEM;
    }
}

esp_err_t mqtt_client_get_stats(uint64_t* connected_time_ms, uint32_t* messages_sent_out,
                               uint32_t* messages_failed_out, esp_err_t* last_error_out) {
    if (connected_time_ms) *connected_time_ms = connect_time_ms;
    if (messages_sent_out) *messages_sent_out = messages_sent;
    if (messages_failed_out) *messages_failed_out = messages_failed;
    if (last_error_out) *last_error_out = last_error;
    return ESP_OK;
}

// Event handler for MQTT client
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                              int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to %s:%d", current_config.broker_host, current_config.broker_port);
            current_state = MQTT_STATE_CONNECTED;
            connect_time_ms = esp_timer_get_time() / 1000;
            last_error = ESP_OK;
            
            if (status_callback) {
                status_callback(true, ESP_OK);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            current_state = MQTT_STATE_DISCONNECTED;
            
            if (status_callback) {
                status_callback(false, ESP_OK);
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            current_state = MQTT_STATE_ERROR;
            last_error = ESP_FAIL;
            
            if (status_callback) {
                status_callback(false, ESP_FAIL);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %d", event_id);
            break;
    }
}

// Task to process queued messages
static void mqtt_queue_task(void* pvParameters) {
    queued_message_t message;
    
    while (1) {
        // Wait for queued messages
        if (xQueueReceive(mqtt_message_queue, &message, portMAX_DELAY) == pdTRUE) {
            // Wait for connection if not connected
            while (current_state != MQTT_STATE_CONNECTED) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            
            // Publish the queued message
            if (mqtt_client_handle) {
                int msg_id = esp_mqtt_client_publish(mqtt_client_handle, message.topic, 
                                                   message.payload, strlen(message.payload),
                                                   message.qos, message.retain ? 1 : 0);
                if (msg_id >= 0) {
                    messages_sent++;
                    ESP_LOGI(TAG, "Published queued message to %s", message.topic);
                } else {
                    messages_failed++;
                    ESP_LOGE(TAG, "Failed to publish queued message to %s", message.topic);
                }
            }
        }
    }
}

// Create JSON payload for bark event
static esp_err_t create_bark_event_json(const bark_event_t* event, char* json_buffer, size_t buffer_size) {
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    // Add event data to JSON
    cJSON_AddNumberToObject(json, "timestamp", (double)event->timestamp_ms);
    cJSON_AddNumberToObject(json, "sequence", event->sequence_num);
    cJSON_AddNumberToObject(json, "confidence", event->confidence);
    cJSON_AddNumberToObject(json, "duration_ms", event->duration_ms);
    cJSON_AddNumberToObject(json, "rms_level", event->rms_level);
    cJSON_AddNumberToObject(json, "peak_level", event->peak_level);
    cJSON_AddStringToObject(json, "device_id", event->device_id);
    cJSON_AddStringToObject(json, "firmware", event->firmware_version);
    cJSON_AddStringToObject(json, "event_type", "dog_bark");

    // Convert to string
    char* json_string = cJSON_Print(json);
    if (!json_string) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    // Copy to buffer
    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(json_buffer, json_string);
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

// Get device MAC address as string
static void get_device_mac_string(char* mac_str, size_t size) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(mac_str, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}