#include "nvs_flash.h"
#include "esp_log.h"

#include "NvsStorageManager.h"

static const char *TAG = "NvsStorageManager";

NvsStorageManager::NvsStorageManager(const std::string& storageNamespace) : ns(storageNamespace) {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        initialized = false;
    } else {
        initialized = true;
    }
}

bool NvsStorageManager::store(const std::string& key, const std::string& value) {
    if (!initialized) return false;

	if (key.size() > 15) {
        ESP_LOGE(TAG, "key longer than 15 chars '%s'", key.c_str());
	}
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    if ((err = nvs_set_str(nvs_handle, key.c_str(), value.c_str())) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store '%s' value '%s' err is '%s'", key.c_str(), value.c_str(), esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err == ESP_OK;
}

bool NvsStorageManager::retrieve(const std::string& key, std::string& value) const {
    if (!initialized)
		return false;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns.c_str(), NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;

    // Query the stored size first rather than guessing a fixed buffer: a
    // value longer than any hardcoded guess would otherwise fail with
    // ESP_ERR_NVS_INVALID_LENGTH, indistinguishable from "key not found".
    size_t value_size = 0;
    err = nvs_get_str(nvs_handle, key.c_str(), nullptr, &value_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Did not get NVS value '%s'", key.c_str());
        return false;
    }

    std::string buf(value_size, '\0');  // value_size includes the null terminator
    err = nvs_get_str(nvs_handle, key.c_str(), buf.data(), &value_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Did not get NVS value '%s'", key.c_str());
        return false;
    }

    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    value = std::move(buf);
    return true;
}

bool NvsStorageManager::clear(const std::string& key) {
    if (!initialized) return false;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_key(nvs_handle, key.c_str());
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err == ESP_OK;
}

bool NvsStorageManager::clearAll() {
    if (!initialized) return false;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns.c_str(), NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err == ESP_OK;
}
