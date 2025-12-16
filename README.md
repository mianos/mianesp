# Simple C++ wrappers around some esp-idf components

## API Endpoints built into the webserver base class

This web server provides the following endpoints:


### 1. Reset WiFi Configuration

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

### 2. Set Hostname

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

### 3. Health Check

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
*   **Error Handling:**  In case of errors, the server will typically respond with a JSON error message including an `error` field describing the issue and a `statusCode` indicating the HTTP status code. For example:
    ```json
    {
      "error": "Missing context",
      "statusCode": 500
    }
    ```
*   **Content-Type:** For `POST` requests that accept JSON data (like `/set_hostname`), ensure you include the `Content-Type: application/json` header in your `curl` command.
*   **Customization:** The `/healthz` endpoint is designed to be extensible. Derived classes can add more fields to the JSON response to provide more specific health information relevant to their application.
