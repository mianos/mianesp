#include "HttpClient.h"
#include <esp_log.h>
#include <utility>

static const char* TAG = "HttpClient";

// Constructor: Only store the URL; defer HTTP client init to each call.
HttpClient::HttpClient(const std::string& url) : url(url) {

}

// Destructor: No persistent client to clean up.
HttpClient::~HttpClient() {

}

// Move constructor: Do not move the client member.
HttpClient::HttpClient(HttpClient&& other) noexcept
    : url(std::move(other.url)),
      timeout(other.timeout),
      responseBuffer(std::move(other.responseBuffer))
{

}

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept {
    if (this != &other) {
        url = std::move(other.url);
        timeout = other.timeout;
        responseBuffer = std::move(other.responseBuffer);
    }
    return *this;
}

// Set the timeout: Only update the member, as client is re-initialized for each call.
void HttpClient::setTimeout(int milliseconds) {
    timeout = milliseconds;
}

// Perform an HTTP GET request
std::pair<bool, std::string> HttpClient::get() {
    // Clear previous response
    responseBuffer.clear();

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = HttpClient::handleHttpEvent;
    config.user_data = this;
    config.timeout_ms = timeout;
    config.method = HTTP_METHOD_GET;

    auto* client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for GET");
        return {false, ""};
    }

    // performRequest handles the execute and cleanup
    return performRequest(client);
}

// Perform an HTTP POST request
std::pair<bool, std::string> HttpClient::post(const std::string& postData) {
    // Clear previous response
    responseBuffer.clear();

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = HttpClient::handleHttpEvent;
    config.user_data = this;
    config.timeout_ms = timeout;
    config.method = HTTP_METHOD_POST;

    auto* client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for POST");
        return {false, ""};
    }

    esp_http_client_set_post_field(client, postData.c_str(), postData.size());

    // performRequest handles the execute and cleanup
    return performRequest(client);
}

// Private helper to execute request and cleanup
std::pair<bool, std::string> HttpClient::performRequest(esp_http_client_handle_t client) {
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP Request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return {false, ""};
    }

    int statusCode = esp_http_client_get_status_code(client);
    // You might want to accept other 2xx codes, but strict 200 is generally fine for REST APIs
    if (statusCode < 200 || statusCode >= 300) {
        ESP_LOGE(TAG, "HTTP Request returned status code: %d", statusCode);
        esp_http_client_cleanup(client);
        return {false, ""};
    }

    std::string result(responseBuffer.begin(), responseBuffer.end());
    esp_http_client_cleanup(client);
    return {true, result};
}

// Handle HTTP events and populate the response buffer
esp_err_t HttpClient::handleHttpEvent(esp_http_client_event_t* evt) {
    auto* client = static_cast<HttpClient*>(evt->user_data);

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && client) {
                client->responseBuffer.insert(
                    client->responseBuffer.end(),
                    static_cast<const char*>(evt->data),
                    static_cast<const char*>(evt->data) + evt->data_len);
            }
            break;

        default:
            break;
    }

    return ESP_OK;
}
