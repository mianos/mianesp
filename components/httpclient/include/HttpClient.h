#pragma once

#include <string>
#include <vector>
#include <utility>
#include <esp_http_client.h>

/**
 * A modern C++ wrapper for ESP-IDF's HTTP client.
 * Supports simple GET and POST requests with proper error handling.
 */
class HttpClient {
public:
    explicit HttpClient(const std::string& url);
    ~HttpClient();

    // Delete copy constructor and assignment operator for safety
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Allow move semantics
    HttpClient(HttpClient&& other) noexcept;
    HttpClient& operator=(HttpClient&& other) noexcept;

    // Set the timeout for the HTTP client
    void setTimeout(int milliseconds);

    /**
     * Sends an HTTP POST request.
     * @param postData The data to send in the POST request.
     * @return Pair containing the success status and the response data.
     */
    std::pair<bool, std::string> post(const std::string& postData);

    /**
     * Sends an HTTP GET request.
     * @return Pair containing the success status and the response data.
     */
    std::pair<bool, std::string> get();

private:
    // Helper to perform the request execution to avoid code duplication
    std::pair<bool, std::string> performRequest(esp_http_client_handle_t client);

    std::string url;
    int timeout = 5000; // Default timeout (milliseconds)
    std::vector<char> responseBuffer;

    static esp_err_t handleHttpEvent(esp_http_client_event_t* evt);
};
