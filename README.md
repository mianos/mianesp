# mianesp — reusable C++ wrappers for ESP-IDF

A small collection of header-first C++ components that wrap common ESP-IDF
subsystems (WiFi provisioning, HTTP server/client, MQTT, JSON, NVS, OTA, GPIO
buttons) behind tidy RAII interfaces. Built and maintained against **ESP-IDF
v6.0.1**.

These components are shared across several device firmwares — e.g.
[`ldr3`](https://github.com/mianos/luxrad) (as `luxrad`),
[`mqttradar`](https://github.com/mianos/mqttradar) and
[`stillerate2`](https://github.com/mianos/stillerate2) — which pull them
directly from this repo via the ESP-IDF component manager.

## Components

| Component | Header | Purpose | Registry / IDF deps |
|---|---|---|---|
| [`jsonwrapper`](components/jsonwrapper) | `JsonWrapper.h` | RAII wrapper over cJSON: parse, build, typed get/add, timestamps | `espressif/cjson` |
| [`nvsstoragemanager`](components/nvsstoragemanager) | `NvsStorageManager.h` | Simple key/value store over NVS with a namespace | `nvs_flash` |
| [`wifimanager`](components/wifimanager) | `WifiManager.h` | WiFi lifecycle + EspTouch V2 (SmartConfig) provisioning, hostname persistence | `esp_wifi`, `espressif/network_provisioning`, `nvsstoragemanager` |
| [`webserver`](components/webserver) | `WebServer.h` | HTTP server base class with async workers and built-in endpoints (see below) | `esp_http_server`, `esp_timer`, `wifimanager`, `jsonwrapper` |
| [`httpclient`](components/httpclient) | `HttpClient.h` | Move-only wrapper over the ESP-IDF HTTP client for simple GET/POST | `esp_http_client` |
| [`mqttwrapper`](components/mqttwrapper) | `MqttClient.h` | MQTT client with regex topic-routing, offline queueing and auto-resubscribe | `espressif/mqtt`, `espressif/cjson`, `nvsstoragemanager`, `jsonwrapper` |
| [`audioplayer`](components/audioplayer) | `AudioPlayer.h` | Streaming MP3-over-HTTP player (GET or JSON POST, e.g. TTS): queue + dedicated task, jitter-buffered, Helix decode to I2S | `esp_driver_i2s`, `esp_http_client`, `chmorgan/esp-libhelix-mp3` |
| [`otawrapper`](components/otawrapper) | `Ota.h` | Background HTTPS OTA with progress callbacks and auto-restart | `esp_https_ota`, `esp_system` |
| [`button`](components/button) | `Button.h` | Debounced GPIO button with long-press detection (header-only) | `driver`, `esp_timer` |

### Inter-component dependencies

`webserver` requires `wifimanager` + `jsonwrapper`; `wifimanager` requires
`nvsstoragemanager`; `mqttwrapper` requires `nvsstoragemanager` + `jsonwrapper`.
When consumed over git, these transitive deps are resolved from **the same repo
on `main`** (see each component's `idf_component.yml`).

## Using these components

Add the ones you need to your project's `main/idf_component.yml`. Each is a
subdirectory of this repo, so use a git dependency with a `path:` and pin to
`main`:

```yaml
dependencies:
  jsonwrapper:
    git: git@github.com:mianos/mianesp.git
    path: components/jsonwrapper
    version: main
  wifimanager:
    git: git@github.com:mianos/mianesp.git
    path: components/wifimanager
    version: main
  webserver:
    git: git@github.com:mianos/mianesp.git
    path: components/webserver
    version: main

  # cJSON is no longer bundled in IDF v6 and mqtt is a managed component;
  # pull them from the Espressif registry. network_provisioning is pulled
  # transitively by wifimanager.
  espressif/cjson: "*"
  espressif/mqtt: "*"
```

> The git URL is SSH (`git@github.com:...`), so the build host (and CI) needs an
> SSH key with read access to the `mianos` repos.

### ESP-IDF v6 notes

- **cJSON** is no longer bundled in IDF v6 — `jsonwrapper` depends on the
  `espressif/cjson` managed component.
- **WiFi provisioning** uses `espressif/network_provisioning` (EspTouch V2 /
  SmartConfig) rather than the removed legacy provisioning APIs.
- **MQTT** is a managed component (`espressif/mqtt`).

## Component reference

### jsonwrapper — `JsonWrapper`

Move-only RAII handle around a `cJSON*` (copies are deleted to avoid
double-free). Highlights:

- `JsonWrapper::Parse(std::string)` — parse a JSON string.
- `AddItem(key, value)` — templated; handles integers, floats (rounded to 4
  decimals), `std::string`, and C strings.
- `GetField(key, out, mandatory=false)` — typed read; returns `true` **only when
  a value was actually written** to `out` (type-checked). The `mandatory` flag
  is retained for caller-side logging but no longer affects the return value.
- `ContainsField(key)`, `Empty()`, `ToString()`, `AddTime(local=true, field)`,
  `Release()`.

### nvsstoragemanager — `NvsStorageManager`

Thin key/value store over NVS scoped to a namespace (default `"storage"`).

```cpp
NvsStorageManager nvs;                 // namespace "storage"
nvs.store("hostname", "my-device");
std::string v;
if (nvs.retrieve("hostname", v)) { /* ... */ }
nvs.clear("hostname");                  // or clearAll()
```

### wifimanager — `WiFiManager`

Owns the WiFi lifecycle and provisioning. Constructed with an
`NvsStorageManager` (for persisted settings such as hostname) and an optional
application event handler:

```cpp
WiFiManager wifi(nvs, myEventHandler, myArg, /*clear_settings=*/false);
std::string host = "my-device";
wifi.configSetHostName(host);
```

Provisioning uses EspTouch V2 (SmartConfig). Empty reserved data from the
provisioning payload will **not** overwrite an existing hostname.

### webserver — `WebServer`

HTTP server base class. Subclass it and override `populate_healthz_fields()` to
add app-specific health data; register additional handlers by overriding
`start()`. Requests are dispatched to a pool of async worker tasks.

```cpp
WebContext ctx(&wifi);
WebServer server(&ctx);
server.start();   // registers the built-in endpoints, then your own
```

#### Built-in endpoints

##### 1. Reset WiFi Configuration

*   **Endpoint:** `/reset`
*   **Method:** `POST`
*   **Description:** Resets the WiFi configuration to default. This will likely clear saved WiFi credentials, forcing the device to re-enter WiFi setup mode (if applicable).
*   **Request Body:** None
*   **Response Body:**
    ```json
    {
      "status": "OK"
    }
    ```
*   **Example `curl` command:**
    ```bash
    curl -X POST http://$DEVICE/reset
    ```
    *Replace `http://$DEVICE` with the actual IP address of your device.*

##### 2. Set Hostname

*   **Endpoint:** `/set_hostname`
*   **Method:** `POST`
*   **Description:** Sets the hostname of the device. This allows you to give your device a custom name on the network.
*   **Request Body:**
    ```json
    {
      "host_name": "your_new_hostname"
    }
    ```
    *Replace `"your_new_hostname"` with the desired hostname. Hostnames should follow standard hostname conventions.*
*   **Response Body:**
    ```json
    {
      "status": "OK",
      "host_name": "your_new_hostname"
    }
    ```
    *The response will echo back the hostname that was successfully set.*
*   **Example `curl` command:**
    ```bash
    curl -X POST -H "Content-Type: application/json" -d '{"host_name": "my-esp32-device"}' http://$DEVICE/set_hostname
    ```
    *Replace `http://$DEVICE` with the actual IP address of your device and `"my-esp32-device"` with your desired hostname.*

##### 3. Health Check

*   **Endpoint:** `/healthz`
*   **Method:** `GET`
*   **Description:**  Provides a health status of the web server and device. This endpoint returns information about uptime and the current time on the device. It may also be extended by derived classes to include other health-related data.
*   **Request Body:** None
*   **Response Body:**
    ```json
    {
      "uptime": 12345,
      "time": "2025-02-09T11:36:59+1100"
      // ... potentially other fields added by derived classes ...
    }
    ```
    *   `uptime`:  The uptime of the device in seconds.
    *   `time`: The current time on the device, formatted as an ISO 8601 timestamp.
    *   *Note:  The response may include additional fields depending on customizations in derived classes.*
*   **Example `curl` command:**
    ```bash
    curl http://$DEVICE/healthz
    ```
    *Replace `http://$DEVICE` with the actual IP address of your device.*

**General Notes:**

*   **`$DEVICE` Placeholder:**  Remember to replace `http://$DEVICE` in the `curl` examples with the actual IP address or hostname of your ESP32 device on your network. You can usually find this IP address in your router's administration panel or via serial monitor output during device startup.
*   **Error Handling:**  In case of errors, the server responds with a JSON error message including an `error` field and a `statusCode` (via `sendJsonError`). For example:
    ```json
    {
      "error": "Missing context",
      "statusCode": 500
    }
    ```
*   **Content-Type:** For `POST` requests that accept JSON data (like `/set_hostname`), ensure you include the `Content-Type: application/json` header in your `curl` command.
*   **Customization:** The `/healthz` endpoint is designed to be extensible. Derived classes override `populate_healthz_fields()` to add more fields to the JSON response.

### httpclient — `HttpClient`

Move-only wrapper over the ESP-IDF HTTP client for one-shot requests.

```cpp
HttpClient client("http://example.com/pump");
client.setTimeout(5000);
auto [ok, body] = client.get();          // or client.post("payload")
if (ok) { /* use body */ }
```

Both `get()` and `post()` return `std::pair<bool, std::string>` — success flag
plus response body.

### mqttwrapper — `MqttClient`

MQTT client with regex-based topic routing, bounded offline queueing and
automatic resubscribe on reconnect.

```cpp
esp_mqtt_client_config_t cfg = {};
cfg.broker.address.uri = "mqtt://mqtt2.mianos.com";
MqttClient mqtt(cfg, "still2");
mqtt.registerHandler("cmnd/still2/settings", std::regex("cmnd/.*/settings"),
    [](MqttClient* c, const std::string& topic, const JsonWrapper& json, void* ctx) {
        return ESP_OK;
    }, /*context=*/nullptr);
mqtt.start();
mqtt.wait_for_connection();
mqtt.publish("tele/still2/state", json.ToString());   // QoS 0 by default
```

- **Telemetry defaults to QoS 0** — periodic data is superseded by the next
  sample, so a dropped message is preferable to a stale retransmit. Pass `qos=1`
  for messages that must not be lost.
- While disconnected, up to `kMaxQueued` (32) messages are buffered; the oldest
  are dropped beyond that so a reconnect can't dump a large backlog of stale
  telemetry.

### otawrapper — `OTAUpdater`

Runs an HTTPS OTA download on a background FreeRTOS task, reports progress at a
configurable percentage step, and restarts on success.

```cpp
OTAUpdater ota("http://ota.mianos.com/still2.bin",
               [](int pct) { ESP_LOGI("app", "OTA %d%%", pct); },
               /*step=*/10);
ota.start();                       // or ota.start("http://.../override.bin")
```

> The default HTTPS config sets `skip_cert_common_name_check = true` and no
> `cert_pem` — suitable for a trusted LAN update server, not the public internet.

### button — `Button`

Header-only debounced GPIO button (active-low, internal pull-up) with long-press
detection. Poll `longPressed()` from your loop; it returns `true` once when the
button has been held for `LONG_PRESS_TIME_MS` (3 s).

```cpp
Button button(GPIO_NUM_9);
if (button.longPressed()) { /* enter provisioning, factory reset, ... */ }
```

## Repository layout

```
components/
  button/            # header-only GPIO long-press
  httpclient/        # esp_http_client wrapper
  jsonwrapper/       # cJSON wrapper
  mqttwrapper/       # esp-mqtt wrapper + topic routing
  nvsstoragemanager/ # NVS key/value store
  otawrapper/        # HTTPS OTA
  webserver/         # esp_http_server base class
  wifimanager/       # WiFi + EspTouch V2 provisioning
idf_components.yaml  # repo manifest (name/version/maintainers)
```
