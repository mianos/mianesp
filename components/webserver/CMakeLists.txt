idf_component_register(SRCS "src/WebServer.cpp"
    INCLUDE_DIRS
      "include"
    REQUIRES
     jsonwrapper
     esp_http_server
     esp_timer
     lwip
)
