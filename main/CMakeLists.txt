idf_component_register(SRCS 
main.cpp
#epaper_RGB_slider.cpp
#epaper_demo.cpp
#sharp_demo.cpp
#touch-test-gt911.c
#touch-test-tt21100.c
INCLUDE_DIRS ${LVGL_INCLUDE_DIRS}

REQUIRES driver esp_timer 
# LVGL specifics
lvgl lvgl_epaper_drivers 
)
