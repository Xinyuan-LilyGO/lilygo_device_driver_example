set(example_requires "common")

if(CONFIG_EXAMPLE_BUILD_AW86224)
    set(example_name "aw86224")
elseif(CONFIG_EXAMPLE_BUILD_WIFI_SCAN_CONNECT_TIME)
    set(example_name "wifi_scan_connect_time")
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
