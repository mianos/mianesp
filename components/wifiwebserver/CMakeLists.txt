idf_component_register(SRCS "src/WifiWebServer.cpp"
    INCLUDE_DIRS
      "include"
    REQUIRES
     webserver
     wifimanager
     jsonwrapper
     esp_http_server
)
