#include "WebServer.h"
#include "JsonWrapper.h"
#include <ctime>
#include <vector>
#include <cstring>
#include <string>
#include "cJSON.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char* TAG = "WebServer";

QueueHandle_t WebServer::async_req_queue = nullptr;
SemaphoreHandle_t WebServer::worker_ready_count = nullptr;
TaskHandle_t WebServer::worker_handles[WebServer::MAX_ASYNC_REQUESTS] = {nullptr};

WebServer::WebServer(WebContext* context)
    : server(nullptr),
      webContext(context) {
    start_async_req_workers();
}

WebServer::~WebServer() {
    stop();
}

esp_err_t WebServer::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;
    config.max_open_sockets = MAX_ASYNC_REQUESTS + 1;
    // Default is 8; derived servers (e.g. robofoc) register more than the base
    // /reset + /set_hostname + /healthz, so give the table generous headroom.
    config.max_uri_handlers = 16;
    config.stack_size = CONFIG_WEBSERVER_TASK_STACK_SIZE;

    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server!");
        return result;
    }

    httpd_uri_t resetWifiUri {
        .uri       = "/reset",
        .method    = HTTP_POST,
        .handler   = reset_wifi_handler,
        .user_ctx  = webContext
    };
    httpd_register_uri_handler(server, &resetWifiUri);

    httpd_uri_t setHostnameUri {
        .uri       = "/set_hostname",
        .method    = HTTP_POST,
        .handler   = set_hostname_handler,
        .user_ctx  = webContext
    };
    httpd_register_uri_handler(server, &setHostnameUri);

    httpd_uri_t healthzUri {
        .uri       = "/healthz",
        .method    = HTTP_GET,
        .handler   = healthz_handler,
        // Instead of storing 'webContext', we store 'this' so we can call
        // populate_healthz_fields(...) in the base or derived class.
        .user_ctx  = this
    };
    httpd_register_uri_handler(server, &healthzUri);

    // The built-in replies are plain text; keep every error in the JSON shape.
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, [](httpd_req_t* req, httpd_err_code_t) {
        return sendJsonError(req, 404, std::string("no such endpoint: ") + req->uri);
    });
    httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, [](httpd_req_t* req, httpd_err_code_t) {
        return sendJsonError(req, 405, std::string("method not allowed on ") + req->uri);
    });

    return ESP_OK;
}

esp_err_t WebServer::stop() {
    if (server != nullptr) {
        esp_err_t result = httpd_stop(server);
        if (result == ESP_OK) {
            server = nullptr;
        }
        return result;
    }
    return ESP_OK;
}

void WebServer::start_async_req_workers() {
    worker_ready_count = xSemaphoreCreateCounting(MAX_ASYNC_REQUESTS, 0);
    if (!worker_ready_count) {
        ESP_LOGE(TAG, "Failed to create counting semaphore");
        return;
    }
    async_req_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (!async_req_queue) {
        ESP_LOGE(TAG, "Failed to create async request queue");
        vSemaphoreDelete(worker_ready_count);
        worker_ready_count = nullptr;
        return;
    }

    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        BaseType_t created = xTaskCreate(
            async_req_worker_task,
            "async_req_worker",
            ASYNC_WORKER_TASK_STACK_SIZE,
            nullptr,
            ASYNC_WORKER_TASK_PRIORITY,
            &worker_handles[i]
        );
        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to start async worker %d", i);
        }
    }
}

void WebServer::async_req_worker_task(void* arg) {
    ESP_LOGI(TAG, "Starting async request worker task");
    while (true) {
        xSemaphoreGive(worker_ready_count);

        httpd_async_req_t asyncReq;
        if (xQueueReceive(async_req_queue, &asyncReq, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Invoking %s", asyncReq.req->uri);
            asyncReq.handler(asyncReq.req);
            if (httpd_req_async_handler_complete(asyncReq.req) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to complete async request");
            }
        }
    }
    vTaskDelete(nullptr);
}

bool WebServer::is_on_async_worker_thread() {
    TaskHandle_t selfHandle = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        if (worker_handles[i] == selfHandle) {
            return true;
        }
    }
    return false;
}

esp_err_t WebServer::submit_async_req(httpd_req_t* req, httpd_req_handler_t handler) {
    httpd_req_t* copied = nullptr;
    esp_err_t error = httpd_req_async_handler_begin(req, &copied);
    if (error != ESP_OK) {
        return error;
    }

    httpd_async_req_t asyncReq;
    asyncReq.req = copied;
    asyncReq.handler = handler;

    if (xSemaphoreTake(worker_ready_count, 0) == pdFALSE) {
        httpd_req_async_handler_complete(copied);
        return ESP_FAIL;
    }
    if (xQueueSend(async_req_queue, &asyncReq, pdMS_TO_TICKS(100)) == pdFALSE) {
        httpd_req_async_handler_complete(copied);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// The status line needs a reason phrase; a bare number is malformed HTTP.
static const char* statusLine(int statusCode) {
    switch (statusCode) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 400: return "400 Bad Request";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 409: return "409 Conflict";
        case 413: return "413 Payload Too Large";
        case 422: return "422 Unprocessable Entity";
        case 503: return "503 Service Unavailable";
        case 504: return "504 Gateway Timeout";
        default:  return "500 Internal Server Error";
    }
}

esp_err_t WebServer::sendJson(httpd_req_t* req, const JsonWrapper& json, int statusCode) {
    httpd_resp_set_status(req, statusLine(statusCode));
    httpd_resp_set_type(req, "application/json");
    const std::string out = json.ToString();
    return httpd_resp_sendstr(req, out.c_str());
}

esp_err_t WebServer::sendJsonError(httpd_req_t* req, int statusCode, const std::string& message) {
    JsonWrapper json;
    json.AddItem("error", message);
    json.AddItem("statusCode", statusCode);
    sendJson(req, json, statusCode);
    return ESP_FAIL;
}

bool WebServer::readJsonBody(httpd_req_t* req, JsonWrapper& out, size_t maxBytes) {
    if (req->content_len <= 0) {
        sendJsonError(req, 400, "JSON body required");
        return false;
    }
    if (static_cast<size_t>(req->content_len) > maxBytes) {
        sendJsonError(req, 413, "request body too large");
        return false;
    }
    std::string body(req->content_len, '\0');
    int received = 0;
    while (received < static_cast<int>(req->content_len)) {
        int got = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
            sendJsonError(req, 400, "request body truncated");
            return false;
        }
        received += got;
    }
    cJSON* parsed = cJSON_Parse(body.c_str());
    const bool isObject = parsed != nullptr && cJSON_IsObject(parsed);
    cJSON_Delete(parsed);
    if (!isObject) {
        sendJsonError(req, 400, "invalid JSON (expected an object)");
        return false;
    }
    out = JsonWrapper::Parse(body);
    return true;
}

std::string WebServer::callerAddress(httpd_req_t* req) {
    struct sockaddr_in6 addr = {};
    socklen_t len = sizeof(addr);
    if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return "?";
    }
    char buf[INET6_ADDRSTRLEN] = {};
    if (addr.sin6_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(&addr)->sin_addr, buf, sizeof(buf));
        return buf;
    }
    inet_ntop(AF_INET6, &addr.sin6_addr, buf, sizeof(buf));
    std::string s(buf);
    // The server socket is dual-stack, so IPv4 peers arrive v4-mapped.
    for (const char* prefix : {"::FFFF:", "::ffff:"}) {
        if (s.rfind(prefix, 0) == 0) return s.substr(7);
    }
    return s;
}

esp_err_t WebServer::reset_wifi_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
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

esp_err_t WebServer::healthz_handler(httpd_req_t* req) {
    auto* server = static_cast<WebServer*>(req->user_ctx);
    if (!server) {
        return sendJsonError(req, 500, "Missing WebServer");
    }
    if (!server->webContext) {
        return sendJsonError(req, 500, "Missing WebContext");
    }

    uint64_t uptimeUs = esp_timer_get_time();
    uint32_t uptimeSec = static_cast<uint32_t>(uptimeUs / 1000000ULL);

    time_t now;
    time(&now);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);

    char timeBuffer[30];
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%dT%H:%M:%S%z", &timeInfo);

    JsonWrapper json;
    json.AddItem("uptime", uptimeSec);
    json.AddItem("time", timeBuffer);

    // Derived classes can override this to add custom fields to 'json'
    server->populate_healthz_fields(server->webContext, json);

    return sendJson(req, json);
}

esp_err_t WebServer::set_hostname_handler(httpd_req_t* req) {
    auto* ctx = static_cast<WebContext*>(req->user_ctx);
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
