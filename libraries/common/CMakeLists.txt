set(requires "lilygo_device_driver" "esp_lcd" "esp_timer")
set(sources "./src/common.cpp")

if(CONFIG_EXAMPLE_BUILD_SCREEN_LVGL OR
   CONFIG_EXAMPLE_BUILD_SCREEN_LVGL_TOUCH_DRAW)
    list(APPEND sources "./src/display/lvgl.cpp")
endif()

if(CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF OR
   CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF_HOST_MSC OR
   CONFIG_EXAMPLE_BUILD_USB_HOST_MSC)
    list(APPEND sources "./src/usb/host.cpp")
endif()

if(CONFIG_EXAMPLE_BUILD_SD_MP3)
    list(APPEND sources "./src/audio/mp3_metadata.cpp")
endif()

if(CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF OR
   CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF_HOST_MSC)
    list(APPEND sources
        "./src/usb/ethernet.cpp"
        "./src/usb/ethernet_transport.cpp")
endif()

idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "./src"
    REQUIRES ${requires}
)

idf_component_optional_requires(PUBLIC lvgl__lvgl)
idf_component_optional_requires(PRIVATE esp_hosted)

if(CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF OR
   CONFIG_EXAMPLE_BUILD_USB_ETHERNET_IPERF_HOST_MSC)
    if(NOT CONFIG_USB_HOST_HUBS_SUPPORTED OR
       NOT CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK)
        message(FATAL_ERROR
            "USB Ethernet requires hub support and enumeration filtering")
    endif()
    idf_component_optional_requires(PUBLIC usb)
    idf_component_optional_requires(PRIVATE
        esp_event
        esp_netif
        esp_hw_support
        espressif__iot_eth
        espressif__iot_usbh_ecm
        espressif__iot_usbh_cdc)
    # 包装仅用于 USB 以太网示例；强制链接，避免静态库丢弃包装函数。
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=usb_host_client_register"
        "-Wl,--wrap=usb_host_transfer_submit"
        "-Wl,--wrap=usbh_cdc_send_custom_request"
        "-Wl,--undefined=__wrap_usb_host_client_register"
        "-Wl,--undefined=__wrap_usb_host_transfer_submit"
        "-Wl,--undefined=__wrap_usbh_cdc_send_custom_request")
endif()
