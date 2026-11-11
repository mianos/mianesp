#pragma once
#include "WebServer.h"
#include "WifiManager.h"

// WebContext for the servers that manage WiFi credentials and hostname.
struct WifiWebContext : WebContext {
    WiFiManager* wifiManager = nullptr;

    explicit WifiWebContext(WiFiManager* manager = nullptr) : wifiManager(manager) {}
};

// WebServer plus the endpoints that need a WiFiManager:
//
//   POST /reset          forget the stored credentials (device back to provisioning)
//   POST /set_hostname   {"host_name": "..."} -> DHCP option 12
//
// Everything else (/healthz, the JSON/body helpers, async workers) comes from
// the network-agnostic WebServer base, so firmware without a radio
// (ESP32-P4 over Ethernet) links that one instead and has no WiFi endpoints.
class WifiWebServer : public WebServer {
public:
    explicit WifiWebServer(WifiWebContext* context);

protected:
    void register_routes() override;

    static esp_err_t reset_wifi_handler(httpd_req_t* req);
    static esp_err_t set_hostname_handler(httpd_req_t* req);
};
