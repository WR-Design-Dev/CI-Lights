idf_component_register(SRCS "app_security.c" "app_ota.c" "app_branding.c" "app_config.c" "app_cpu.c" "app_network.c" "app_settings.c" "app_web_assets.c" "factory_reset.c" "jenkins_client.c" "main.c" "traffic_light.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "assets/TrafficLight.svg" "assets/localization.js" "assets/ota.js" "assets/wifi.js" "assets/security.js"
REQUIRES app_update bootloader_support esp_app_format esp_driver_gpio esp_driver_rmt esp_http_client esp_http_server esp_timer esp_pm
                                esp_event esp_hw_support esp_netif esp_system esp_wifi heap spi_flash wpa_supplicant cjson lwip mbedtls mdns nvs_flash esp_partition efuse)

if(CONFIG_APP_SECURITY_SECURE)
    execute_process(COMMAND "${PYTHON}" "${PROJECT_DIR}/tools/generate_security_header.py"
                    --keyring "${CI_LIGHTS_KEYRING}" --output "${CMAKE_CURRENT_BINARY_DIR}/generated/app_secure_keys.h"
                    RESULT_VARIABLE security_header_result)
    if(NOT security_header_result EQUAL 0)
        message(FATAL_ERROR "Secure public key registry is missing or invalid")
    endif()
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
