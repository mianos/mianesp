#pragma once
#include <string>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "JsonWrapper.h"

// Application state shared with the handlers. The base server needs nothing
// from the network stack, so this carries nothing: subclasses add the pointers
// their handlers need (see WifiWebContext in wifiwebserver).
struct WebContext {
    virtual ~WebContext() = default;
};

class WebServer {
public:
    WebServer(WebContext* context);
    virtual ~WebServer();

    virtual esp_err_t start();
    esp_err_t stop();

protected:
    virtual void populate_healthz_fields(WebContext *ctx, JsonWrapper& json) {}

    // URIs registered here are added by start(), after /healthz. Overrides
    // should call WebServer::register_routes() first. (wifiwebserver uses this
    // for /reset and /set_hostname, so firmware without a radio links a server
    // with no WiFi endpoints at all.)
    virtual void register_routes();

    static esp_err_t healthz_handler(httpd_req_t* req);

    // REST conventions shared by every device: success is 2xx with the
    // resulting state as the body; failure is {"error", "statusCode"}.
	static esp_err_t sendJsonError(httpd_req_t* req, int statusCode, const std::string& message);
    static esp_err_t sendJson(httpd_req_t* req, const JsonWrapper& json, int statusCode = 200);
    // Reads and parses a JSON object body. On failure the error reply (400 or
    // 413) has already been sent and false is returned.
    static bool readJsonBody(httpd_req_t* req, JsonWrapper& out, size_t maxBytes = 4096);
    // Peer address of the request, for logging who changed what.
    static std::string callerAddress(httpd_req_t* req);

    httpd_handle_t server;
    WebContext* webContext;

private:
    static constexpr int ASYNC_WORKER_TASK_PRIORITY = 5;
    static constexpr int ASYNC_WORKER_TASK_STACK_SIZE = 4096;
    static constexpr int MAX_ASYNC_REQUESTS = 5;

    using httpd_req_handler_t = esp_err_t (*)(httpd_req_t* req);

    struct httpd_async_req_t {
        httpd_req_t* req;
        httpd_req_handler_t handler;
    };

    static QueueHandle_t async_req_queue;
    static SemaphoreHandle_t worker_ready_count;
    static TaskHandle_t worker_handles[MAX_ASYNC_REQUESTS];

    static void async_req_worker_task(void* arg);
    static bool is_on_async_worker_thread();
    static esp_err_t submit_async_req(httpd_req_t* req, httpd_req_handler_t handler);
    static void start_async_req_workers();
};

