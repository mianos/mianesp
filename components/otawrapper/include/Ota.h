#pragma once

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <functional>
#include <string>

class OTAUpdater {
public:
    std::string url;
    std::function<void(int)> progress_callback;
    int progress_step;  // Progress reporting interval

    // Constructor with progress callback and percentage step (default 10%)
    OTAUpdater(const std::string& ota_url, std::function<void(int)> callback, int step = 10)
        : url(ota_url), progress_callback(std::move(callback)),
          progress_step(step > 0 ? step : 10) { }

    void start(const std::string& optional_url = "") {
        pending_url = optional_url.empty() ? url : optional_url;

        xTaskCreate([](void* obj) {
            auto* updater = static_cast<OTAUpdater*>(obj);
            updater->perform_update(updater->pending_url);
            vTaskDelete(nullptr);
        }, "ota_update_task", 8192, this, 5, nullptr);
    }

    void perform_update(const std::string& ota_url) {
        esp_http_client_config_t http_config = {};
        http_config.url = ota_url.c_str();
        // esp_https_ota_begin() refuses a config that offers it no way to verify
        // the server: no cert_pem, no bundle and no secure element is
        // ESP_ERR_INVALID_ARG ("No option for server verification is enabled"),
        // before it even opens the socket. Attaching the CA bundle fixes HTTPS
        // properly and makes no difference over plain http, so it is always set.
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
        http_config.timeout_ms = 15000;

        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &http_config;

        ESP_LOGI("OTA", "Starting OTA from URL: %s", ota_url.c_str());

        esp_https_ota_handle_t ota_handle = nullptr;
        esp_err_t ret = esp_https_ota_begin(&ota_config, &ota_handle);

        if (ret != ESP_OK) {
            ESP_LOGE("OTA", "Failed to begin OTA: %s", esp_err_to_name(ret));
            return;
        }

        int last_reported_progress = -progress_step;  // Ensure 0% is reported

        while (true) {
            ret = esp_https_ota_perform(ota_handle);

            if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                int image_size = esp_https_ota_get_image_size(ota_handle);
                int received_size = esp_https_ota_get_image_len_read(ota_handle);

                if (image_size > 0) {
                    int progress_percent = (received_size * 100) / image_size;

                    // Report only at progress_step intervals
                    if (progress_callback &&
                        progress_percent / progress_step != last_reported_progress / progress_step) {
                        progress_callback((progress_percent / progress_step) * progress_step);
                        last_reported_progress = progress_percent;
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(50));
            } else if (ret == ESP_OK) {
                if (progress_callback) progress_callback(100);  // Ensure 100% is reported
                ESP_LOGI("OTA", "Download complete");
                break;
            } else {
                ESP_LOGE("OTA", "OTA perform failed: %s", esp_err_to_name(ret));
                esp_https_ota_abort(ota_handle);
                return;
            }
        }

        // perform() can return ESP_OK with the connection cut short, leaving an
        // image that only fails at finish(). Check the byte count first, so the
        // cause is named accurately in the log.
        if (!esp_https_ota_is_complete_data_received(ota_handle)) {
            ESP_LOGE("OTA", "Download truncated; image not complete");
            esp_https_ota_abort(ota_handle);
            return;
        }

        if (esp_https_ota_finish(ota_handle) == ESP_OK) {
            ESP_LOGI("OTA", "OTA successful, restarting...");
            esp_restart();
        } else {
            ESP_LOGE("OTA", "OTA finish failed or image invalid");
        }
    }

private:
    std::string pending_url;
};

