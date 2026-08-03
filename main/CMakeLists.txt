set(example_requires "common")

if(CONFIG_EXAMPLE_BUILD_HAPTIC_FEEDBACK)
    set(example_name "haptic_feedback")
elseif(CONFIG_EXAMPLE_BUILD_IMU)
    set(example_name "imu")
    set(example_extra_sources
        "examples/imu/bhi260ap_qmc6310n.cpp"
        "examples/imu/icm20948.cpp")
elseif(CONFIG_EXAMPLE_BUILD_GPS)
    set(example_name "gps")
    set(example_extra_sources
        "examples/gps/gps.cpp"
        "examples/gps/l76k.cpp"
        "examples/gps/nrf9151.cpp")
elseif(CONFIG_EXAMPLE_BUILD_LORA_RX_SENSITIVITY)
    set(example_name "lora_rx_sensitivity")
    set(example_extra_sources
        "examples/lora_rx_sensitivity/lora_rx_sensitivity.cpp"
        "examples/lora_rx_sensitivity/lr2021.cpp"
        "examples/lora_rx_sensitivity/lr1121.cpp"
        "examples/lora_rx_sensitivity/sx1262.cpp")
elseif(CONFIG_EXAMPLE_BUILD_LORA_TX_RX)
    set(example_name "lora_tx_rx")
    set(example_extra_sources
        "examples/lora_tx_rx/lr2021.cpp"
        "examples/lora_tx_rx/lr1121.cpp"
        "examples/lora_tx_rx/sx1262.cpp")
elseif(CONFIG_EXAMPLE_BUILD_RTC)
    set(example_name "rtc")
elseif(CONFIG_EXAMPLE_BUILD_BATTERY_MANAGEMENT)
    set(example_name "battery_management")
    set(example_extra_sources
        "examples/battery_management/bq27220.cpp"
        "examples/battery_management/axp517.cpp")
elseif(CONFIG_EXAMPLE_BUILD_MICROPHONE_SPEAKER_LOOPBACK)
    set(example_name "microphone_speaker_loopback")
    set(example_extra_sources
        "examples/microphone_speaker_loopback/es8311.cpp"
        "examples/microphone_speaker_loopback/es8389.cpp")
elseif(CONFIG_EXAMPLE_BUILD_SD_MP3)
    set(example_name "sd_mp3")
    set(example_extra_sources
        "examples/sd_mp3/es8311.cpp"
        "examples/sd_mp3/es8389.cpp")
elseif(CONFIG_EXAMPLE_BUILD_WIFI_MP3)
    set(example_name "wifi_mp3")
    set(example_extra_sources
        "examples/wifi_mp3/es8311.cpp"
        "examples/wifi_mp3/es8389.cpp")
elseif(CONFIG_EXAMPLE_BUILD_WIFI_SCAN_CONNECT_TIME)
    set(example_name "wifi_scan_connect_time")
elseif(CONFIG_EXAMPLE_BUILD_COPROCESSOR_DOWNLOAD_MODE)
    set(example_name "coprocessor_download_mode")
elseif(CONFIG_EXAMPLE_BUILD_NETWORK_ADAPTER_UPDATE)
    set(example_name "network_adapter_update")
elseif(CONFIG_EXAMPLE_BUILD_OTA_UPDATE)
    set(example_name "ota_update")
elseif(CONFIG_EXAMPLE_BUILD_I2C_SCAN)
    set(example_name "i2c_scan")
elseif(CONFIG_EXAMPLE_BUILD_SCREEN_CAMERA)
    set(example_name "screen_camera")
elseif(CONFIG_EXAMPLE_BUILD_SCREEN_LVGL)
    set(example_name "screen_lvgl")
    set(example_extra_sources "examples/screen_lvgl/lvgl_demo_ui.c")
elseif(CONFIG_EXAMPLE_BUILD_SCREEN_LVGL_TOUCH_DRAW)
    set(example_name "screen_lvgl_touch_draw")
elseif(CONFIG_EXAMPLE_BUILD_SGM38121)
    set(example_name "sgm38121")
elseif(CONFIG_EXAMPLE_BUILD_ST25R3916)
    set(example_name "st25r3916")
    list(APPEND example_requires "stsw-st25rfal002_cpp_bus_driver")
elseif(CONFIG_EXAMPLE_BUILD_SLEEP)
    set(example_name "sleep")
elseif(CONFIG_EXAMPLE_BUILD_XL9535)
    set(example_name "xl9535")
endif()

set(example_path "examples/${example_name}")
set(example_sources "${example_path}/main.cpp" ${example_extra_sources})

idf_component_register(
    SRCS ${example_sources}
    INCLUDE_DIRS ${example_path}
    REQUIRES ${example_requires}
)

if(CONFIG_EXAMPLE_BUILD_SLEEP)
    idf_component_optional_requires(PRIVATE
        esp_driver_uart
        esp_hw_support
    )
endif()

if(CONFIG_EXAMPLE_BUILD_OTA_UPDATE)
    idf_component_optional_requires(PRIVATE
        app_update
        esp_app_format
        esp_event
        esp_http_client
        esp_https_ota
        esp_netif
        esp_system
        esp_wifi
        mbedtls
    )
endif()

if(CONFIG_EXAMPLE_BUILD_WIFI_MP3)
    idf_component_optional_requires(PRIVATE
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        esp_wifi
        mbedtls
    )
endif()

if(CONFIG_EXAMPLE_BUILD_NETWORK_ADAPTER_UPDATE)
    idf_component_optional_requires(PRIVATE
        bootloader_support
        esp_app_format
        esp_event
        esp_system
    )

    if(CONFIG_SLAVE_IDF_TARGET_ESP32C5)
        set(network_adapter_chip "ESP32-C5")
        set(network_adapter_esptool_chip "esp32c5")
        set(network_adapter_firmware_path
            "firmware/[network_adapter][sdio]/[esp32c5][network_adapter_v2.12.3][sdio]_firmware_202606181430.bin")
    elseif(CONFIG_SLAVE_IDF_TARGET_ESP32C6)
        set(network_adapter_chip "ESP32-C6")
        set(network_adapter_esptool_chip "esp32c6")
        set(network_adapter_firmware_path
            "firmware/[network_adapter][sdio]/[esp32c6][network_adapter_v2.12.3][sdio]_firmware_202606150919.bin")
    else()
        message(FATAL_ERROR
            "No supported Wi-Fi Remote slave target selected")
    endif()

    if(NOT IS_ABSOLUTE "${network_adapter_firmware_path}")
        idf_build_get_property(project_dir PROJECT_DIR)
        get_filename_component(network_adapter_firmware_path
            "${network_adapter_firmware_path}"
            ABSOLUTE BASE_DIR "${project_dir}")
    endif()

    if(NOT EXISTS "${network_adapter_firmware_path}")
        message(FATAL_ERROR
            "${network_adapter_chip} network adapter firmware not found: "
            "${network_adapter_firmware_path}")
    endif()

    partition_table_get_partition_info(network_adapter_firmware_partition_size
                                       "--partition-name storage" "size")
    if(NOT network_adapter_firmware_partition_size)
        message(FATAL_ERROR
            "The selected partition CSV does not contain a "
            "storage partition")
    endif()

    file(SIZE "${network_adapter_firmware_path}"
         network_adapter_firmware_size)
    set(network_adapter_firmware_prefix_size 4096)
    math(EXPR network_adapter_flash_image_size
        "${network_adapter_firmware_size} + ${network_adapter_firmware_prefix_size}")
    if(network_adapter_flash_image_size GREATER
       network_adapter_firmware_partition_size)
        message(FATAL_ERROR
            "${network_adapter_chip} firmware "
            "container (${network_adapter_flash_image_size} bytes) exceeds "
            "storage "
            "partition (${network_adapter_firmware_partition_size} bytes)")
    endif()

    idf_build_get_property(build_dir BUILD_DIR)
    idf_build_get_property(python PYTHON)
    idf_component_get_property(esptool_py_dir esptool_py COMPONENT_DIR)
    set(network_adapter_littlefs_dir
        "${build_dir}/network_adapter_littlefs")
    set(network_adapter_flash_image
        "${network_adapter_littlefs_dir}/network_adapter_firmware.bin")

    add_custom_command(
        OUTPUT "${network_adapter_flash_image}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${network_adapter_littlefs_dir}"
        COMMAND "${python}"
            "${esptool_py_dir}/esptool/esptool.py"
            --chip "${network_adapter_esptool_chip}"
            merge_bin
            --output "${network_adapter_flash_image}"
            --format raw
            --target-offset 0x0
            0x1000 "${network_adapter_firmware_path}"
        DEPENDS "${network_adapter_firmware_path}"
        COMMENT "Packaging ${network_adapter_chip} firmware as P4 flash data"
        VERBATIM
    )
    add_custom_target(network_adapter_firmware_image ALL
        DEPENDS "${network_adapter_flash_image}")

    littlefs_create_partition_image(storage
        "${network_adapter_littlefs_dir}"
        FLASH_IN_PROJECT
        DEPENDS "${network_adapter_flash_image}")

    # Keep the previous behavior where app-flash also installs the bundled
    # network adapter firmware, not just the ESP32-P4 application image.
    esptool_py_flash_to_partition(app-flash "storage"
        "${build_dir}/storage.bin")
    add_dependencies(app-flash littlefs_storage_bin)

    message(STATUS
        "${network_adapter_chip} firmware container will be flashed to "
        "storage:/network_adapter_firmware.bin: "
        "${network_adapter_firmware_path}")
endif()
