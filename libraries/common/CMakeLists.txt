set(requires "lilygo_device_driver" "esp_lcd" "esp_timer")
set(sources "./src/common.cpp")

if(CONFIG_EXAMPLE_BUILD_SCREEN_LVGL OR
   CONFIG_EXAMPLE_BUILD_SCREEN_LVGL_TOUCH_DRAW)
    list(APPEND sources "./src/common_lvgl.cpp")
endif()

idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS "./src"
    REQUIRES ${requires}
)

idf_component_optional_requires(PUBLIC lvgl__lvgl)
