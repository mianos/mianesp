
#include <ctime>
#include <vector>
#include <cstring>
#include <string>
#include "esp_random.h"
#include "esp_timer.h"

#include "JsonWrapper.h"
#include "WebServer.h"

static const char *TAG = "WebServer";

// Define static variables
QueueHandle_t WebServer::async_req_queue = nullptr;
SemaphoreHandle_t WebServer::worker_ready_count = nullptr;
TaskHandle_t WebServer::worker_handles[WebServer::MAX_ASYNC_REQUESTS] = {nullptr};

// Constructor and Destructor
WebServer::WebServer(WebContext* webContext) : webContext(webContext), server(nullptr) {
    start_async_req_workers();
}

WebServer::~WebServer() {
    stop();
}

// Helper method to extract context
WebServer* WebServer::get_context(httpd_req_t* req) {
    auto* ws = static_cast<WebServer*>(req->user_ctx);
    if (!ws) {
        ESP_LOGE(TAG, "ctx null?");
        httpd_resp_send_500(req);
        return nullptr;
    }
    return ws;
}

// Start server
esp_err_t WebServer::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.server_port = 80;
    config.max_open_sockets = MAX_ASYNC_REQUESTS + 1;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error starting server!");
        return ret;
    }

    httpd_uri_t reset_wifi_uri = {
        .uri       = "/reset",
        .method    = HTTP_POST,
        .handler   = reset_wifi_handler,
        .user_ctx  = this
    };

    httpd_uri_t healthz_uri = {
        .uri       = "/healthz",
        .method    = HTTP_GET,
        .handler   = healthz_handler,
        .user_ctx  = this
    };

    httpd_register_uri_handler(server, &reset_wifi_uri);
    httpd_register_uri_handler(server, &healthz_uri);

    return ESP_OK;
}

// Stop server
esp_err_t WebServer::stop() {
    if (server != nullptr) {
        esp_err_t ret = httpd_stop(server);
        if (ret == ESP_OK) {
            server = nullptr;
        }
        return ret;
    }
    return ESP_OK;
}

// Start asynchronous request workers
void WebServer::start_async_req_workers() {
    // Create counting semaphore
    worker_ready_count = xSemaphoreCreateCounting(MAX_ASYNC_REQUESTS, 0);
    if (worker_ready_count == nullptr) {
        ESP_LOGE(TAG, "Failed to create workers counting semaphore");
        return;
    }
    async_req_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (async_req_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create async request queue");
        vSemaphoreDelete(worker_ready_count);
        return;
    }

    // Start worker tasks
    for (int i = 0; i < MAX_ASYNC_REQUESTS; ++i) {
        BaseType_t success = xTaskCreate(
            async_req_worker_task,
            "async_req_worker",
            ASYNC_WORKER_TASK_STACK_SIZE,
            nullptr,
            ASYNC_WORKER_TASK_PRIORITY,
            &worker_handles[i]
        );

        if (success != pdPASS) {
            ESP_LOGE(TAG, "Failed to start async request worker");
            continue;
        }
    }
}

// Async request worker task
void WebServer::async_req_worker_task(void* arg) {
    ESP_LOGI(TAG, "Starting async request worker task");

    while (true) {
        // Signal that a worker is ready
        xSemaphoreGive(worker_ready_count);

        // Wait for a request
        httpd_async_req_t async_req;
        if (xQueueReceive(async_req_queue, &async_req, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Invoking %s", async_req.req->uri);

            // Call the handler
            async_req.handler(async_req.req);

            // Complete the asynchronous request
            if (httpd_req_async_handler_complete(async_req.req) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to complete async request");
            }
        }
    }

    vTaskDelete(nullptr);
}

// Handlers
esp_err_t WebServer::reset_wifi_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "uri: /reset");
    WebServer* ws = get_context(req);
    if (!ws) {
        return ESP_FAIL;
    }

    ws->webContext->wifiManager->clear();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"OK\"}");

    return ESP_OK;
}

esp_err_t WebServer::healthz_handler(httpd_req_t *req) {
    WebServer* ws = get_context(req);
    if (!ws) {
        return ESP_FAIL;
    }

    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = static_cast<uint32_t>(uptime_us / 1000000ULL);

    time_t now;
    time(&now);
    struct tm time_info;
    localtime_r(&now, &time_info);

    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S%z", &time_info);

    JsonWrapper json;
    json.AddItem("uptime", uptime_sec);
    json.AddItem("time", time_str);

    std::string json_str = json.ToString();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str.c_str());
    return ESP_OK;
}

