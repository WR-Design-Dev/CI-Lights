idf_component_register(SRCS "app_ota.c" "app_branding.c" "app_config.c" "app_cpu.c" "app_network.c" "app_settings.c" "app_web_assets.c" "factory_reset.c" "jenkins_client.c" "main.c" "traffic_light.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "assets/TrafficLight.svg" "assets/localization.js" "assets/ota.js" "assets/wifi.js"
REQUIRES app_update bootloader_support esp_app_format esp_driver_gpio esp_driver_rmt esp_http_client esp_http_server esp_timer esp_pm
                                esp_event esp_hw_support esp_netif esp_system esp_wifi heap spi_flash wpa_supplicant cjson lwip mbedtls mdns nvs_flash esp_partition)
