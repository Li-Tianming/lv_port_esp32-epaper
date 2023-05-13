idf_component_register(SRCS 
main.cpp
#epaper_RGB_slider.cpp
#epaper_demo.cpp

INCLUDE_DIRS ${LVGL_INCLUDE_DIRS}

REQUIRES driver esp_timer 
# LVGL specifics
lvgl 
)
