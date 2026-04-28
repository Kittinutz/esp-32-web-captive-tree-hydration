cmake_minimum_required(VERSION 3.16)

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        mqtt
        esp_adc
        driver
        esp_timer
        esp_system
)
