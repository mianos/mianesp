#pragma once

#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "esp_log.h"

#include "JsonWrapper.h"
#include "NvsStorageManager.h"

// NVS-backed application settings: the machinery only, no schema.
//
// An app subclasses SettingsBase, declares its settings as plain members with
// compiled-in defaults, registers each with field() in its constructor, and
// finishes with load():
//
//   struct Settings : SettingsBase {
//       std::string mqttServer = "mqtt.example.com";
//       int         volume     = 100;
//       explicit Settings(NvsStorageManager& nvs) : SettingsBase(nvs) {
//           field("mqtt_server", mqttServer);
//           field("volume",     volume);
//           load();
//       }
//   };
//
// The member value at registration time is captured as the default that
// resetToDefaults() restores. Persistence is a single JSON blob in NVS;
// missing or unparseable config falls back to the defaults (logged).
//
// onChange(key, cb) attaches a callback fired when loadFromJson() or
// resetToDefaults() actually changes that field — attach after construction
// when the callback captures objects built later (e.g. an audio player).
class SettingsBase {
public:
    explicit SettingsBase(NvsStorageManager& nvs, std::string nvsKey = "config")
        : nvs_(nvs), key_(std::move(nvsKey)) {}

    // Apply the stored JSON blob, if any. Call at the end of the subclass
    // constructor, after every field() registration.
    void load() {
        std::string raw;
        if (nvs_.retrieve(key_, raw) && !raw.empty()) {
            JsonWrapper doc = JsonWrapper::Parse(raw);
            if (!doc.Empty()) {
                loadFromJson(doc);
            } else {
                ESP_LOGW(TAG, "stored config unparseable, using defaults");
            }
        } else {
            ESP_LOGI(TAG, "no stored config, using defaults");
        }
    }

    // Apply whichever registered fields are present in `doc`.
    void loadFromJson(const JsonWrapper& doc) {
        for (auto& f : fields_) {
            visit(f, [&](auto& ref, auto& /*def*/) {
                auto v = ref;
                if (doc.GetField(f.key, v) && v != ref) {
                    ref = v;
                    if (f.changed) f.changed();
                }
            });
        }
    }

    JsonWrapper toJson() const {
        JsonWrapper doc;
        for (auto& f : fields_) {
            visit(f, [&](auto& ref, auto&) { doc.AddItem(f.key, ref); });
        }
        return doc;
    }

    void save() const {
        if (!nvs_.store(key_, toJson().ToString())) {
            ESP_LOGE(TAG, "failed to persist settings");
        }
    }

    // Restore every field to its value at registration time.
    void resetToDefaults() {
        for (auto& f : fields_) {
            visit(f, [&](auto& ref, auto& def) {
                if (ref != def) {
                    ref = def;
                    if (f.changed) f.changed();
                }
            });
        }
    }

    void log() const {
        std::string line;
        for (auto& f : fields_) {
            if (!line.empty()) line += ' ';
            line += f.key + '=';
            visit(f, [&](auto& ref, auto&) {
                if constexpr (std::is_same_v<std::decay_t<decltype(ref)>, std::string>) {
                    line += ref.empty() ? "(unset)" : ref;
                } else {
                    line += std::to_string(ref);
                }
            });
        }
        ESP_LOGI(TAG, "%s", line.c_str());
    }

    void onChange(const std::string& key, std::function<void()> cb) {
        for (auto& f : fields_) {
            if (f.key == key) {
                f.changed = std::move(cb);
                return;
            }
        }
        ESP_LOGE(TAG, "onChange: no field '%s'", key.c_str());
    }

protected:
    void field(const char* key, std::string& ref) { fields_.push_back({key, &ref, ref, {}}); }
    void field(const char* key, int& ref)         { fields_.push_back({key, &ref, ref, {}}); }

private:
    static constexpr const char* TAG = "settings";

    struct Field {
        std::string                      key;
        std::variant<std::string*, int*> ref;   // the subclass member
        std::variant<std::string, int>   def;   // its value at registration
        std::function<void()>            changed;
    };

    // Calls fn(memberRef, defaultValue) with matching concrete types.
    template <typename Fn>
    static void visit(auto& f, Fn&& fn) {
        if (auto* s = std::get_if<std::string*>(&f.ref)) {
            fn(**s, std::get<std::string>(f.def));
        } else {
            fn(*std::get<int*>(f.ref), std::get<int>(f.def));
        }
    }

    std::vector<Field> fields_;
    NvsStorageManager& nvs_;
    std::string        key_;
};
