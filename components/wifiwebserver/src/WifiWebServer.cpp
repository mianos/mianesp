#include "WifiWebServer.h"

#include <string>

#include "esp_log.h"

static const char* TAG = "WifiWebServer";

WifiWebServer::WifiWebServer(WifiWebContext* context)
    : WebServer(context) {}

void WifiWebServer::register_routes() {
    WebServer::register_routes();

    httpd_uri_t resetWifiUri {
        .uri       = "/reset",
        .method    = HTTP_POST,
        .handler   = reset_wifi_handler,
        .user_ctx  = webContext
    };
    esp_err_t result = httpd_register_uri_handler(server, &resetWifiUri);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "register /reset: %s", esp_err_to_name(result));
        return;
    }

    httpd_uri_t setHostnameUri {
        .uri       = "/set_hostname",
        .method    = HTTP_POST,
        .handler   = set_hostname_handler,
        .user_ctx  = webContext
    };
    result = httpd_register_uri_handler(server, &setHostnameUri);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "register /set_hostname: %s", esp_err_to_name(result));
    }
}

esp_err_t WifiWebServer::reset_wifi_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WifiWebContext*>(req->user_ctx);
    if (!ctx) {
        ESP_LOGE(TAG, "No valid WebContext");
        return sendJsonError(req, 500, "Missing context");
    }
    if (!ctx->wifiManager) {
        ESP_LOGE(TAG, "No valid wifiManager");
        return sendJsonError(req, 500, "Missing wifiManager");
    }

    ESP_LOGW(TAG, "POST /reset from %s: clearing Wi-Fi credentials", callerAddress(req).c_str());
    ctx->wifiManager->clear();
    JsonWrapper response;
    response.AddItem("wifi_cleared", true);
    return sendJson(req, response);
}

esp_err_t WifiWebServer::set_hostname_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WifiWebContext*>(req->user_ctx);
    if (!ctx) {
        ESP_LOGE(TAG, "No valid WebContext");
        return sendJsonError(req, 500, "Missing context");
    }
    if (!ctx->wifiManager) {
        ESP_LOGE(TAG, "No valid wifiManager");
        return sendJsonError(req, 500, "Missing wifiManager");
    }
    JsonWrapper json;
    if (!readJsonBody(req, json)) return ESP_FAIL;

    std::string host_name;
    if (!json.GetField("host_name", host_name, true)) {
        return sendJsonError(req, 400, "Missing or invalid 'host_name'");
    }

    ESP_LOGW(TAG, "POST /set_hostname from %s: %s", callerAddress(req).c_str(), host_name.c_str());
    ctx->wifiManager->configSetHostName(host_name);

    JsonWrapper response;
    response.AddItem("host_name", host_name);
    return sendJson(req, response);
}
