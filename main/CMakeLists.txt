set(example_requires "common")

if(CONFIG_EXAMPLE_BUILD_AW86224)
    set(example_name "aw86224")
elseif(CONFIG_EXAMPLE_BUILD_WIFI_SCAN_CONNECT_TIME)
    set(example_name "wifi_scan_connect_time")
elseif(CONFIG_EXAMPLE_BUILD_NETWORK_ADAPTER_UPDATE)
    set(example_name "network_adapter_update")
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

if(CONFIG_EXAMPLE_BUILD_NETWORK_ADAPTER_UPDATE)
    idf_component_optional_requires(PRIVATE
        bootloader_support
        esp_app_format
        esp_event
        esp_partition
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
                                       "--partition-name coprocessor_fw" "size")
    if(NOT network_adapter_firmware_partition_size)
        message(FATAL_ERROR
            "The selected partition CSV does not contain a "
            "coprocessor_fw partition")
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
            "coprocessor_fw "
            "partition (${network_adapter_firmware_partition_size} bytes)")
    endif()

    idf_build_get_property(build_dir BUILD_DIR)
    idf_build_get_property(python PYTHON)
    idf_component_get_property(esptool_py_dir esptool_py COMPONENT_DIR)
    set(network_adapter_flash_image
        "${build_dir}/network_adapter_firmware.bin")

    add_custom_command(
        OUTPUT "${network_adapter_flash_image}"
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

    esptool_py_flash_to_partition(flash "coprocessor_fw"
        "${network_adapter_flash_image}")
    esptool_py_flash_to_partition(app-flash "coprocessor_fw"
        "${network_adapter_flash_image}")
    add_dependencies(flash network_adapter_firmware_image)
    add_dependencies(app-flash network_adapter_firmware_image)

    message(STATUS
        "${network_adapter_chip} firmware container will be flashed to "
        "coprocessor_fw: "
        "${network_adapter_firmware_path}")
endif()
