/* LVGL Example project for to test new File explorer
 * ENABLE IT: LVGL configuration -> Others -> File explorer
 * Optional: When using v7 Kaleido PCB, trigger PWM from Front-Light (FL)
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// This example uses SDMMC peripheral to communicate with SD card.
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

// FreeRTOS related & IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_freertos_hooks.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// LVGL
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

extern "C"
{
    void app_main();
    // Our custom lv_file_explorer
    #include "lv_file_explorer.c"
    //#include "include/lv_file_explorer.h"
}

/*********************
 *      DEFINES
 *********************/
#define EXAMPLE_MAX_CHAR_SIZE 64
#define LV_EXPLORER_SORT_KIND LV_EXPLORER_SORT_NONE

lv_obj_t * tab_main_view;
lv_obj_t * tab_main;
lv_obj_t * tab_settings;
lv_obj_t * tab_open_file;
lv_obj_t * switch_label;
uint8_t led_duty_multiplier = 80;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void create_demo_application(void);

#define DISPLAY_FRONTLIGHT    GPIO_NUM_11

#define LV_TICK_PERIOD_MS 1
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          int(DISPLAY_FRONTLIGHT)
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (0) // 4096 Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

/**********************
 *   APPLICATION MAIN
 **********************/
void app_main() {
    printf("Epaper example. LVGL version %d.%d\n\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);

    //printf("app_main started. DISP_BUF_SIZE:%d LV_HOR_RES_MAX:%d V_RES_MAX:%d\n", DISP_BUF_SIZE, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    gpio_set_direction(DISPLAY_FRONTLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_FRONTLIGHT, 0);
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
        // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = LEDC_OUTPUT_IO,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, NULL, 0, NULL, 1);
}

static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();
    /* PLEASE NOTE:
       This size must much the size of DISP_BUF_SIZE declared on lvgl_helpers.h
    */
    printf("DISP_BUF*sizeof(lv_color_t) %d", DISP_BUF_SIZE * sizeof(lv_color_t));
    lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    
    // OPTIONAL: Do not use double buffer for epaper
    //lv_color_t* buf2 = NULL;
    lv_color_t* buf2 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 != NULL);
    
    //size_in_px /= 8; // In v9 size is in bytes
    lv_display_t * disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t) disp_driver_flush);

    printf("LV ROTATION:%d\n\n",lv_display_get_rotation(disp));
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    // COLOR SETTING after v9:
    // LV_COLOR_FORMAT_L8 = monochrome 8BPP (8 bits per pixel)
    // LV_COLOR_FORMAT_RGB332
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB332);

    /**MODE
     * LV_DISPLAY_RENDER_MODE_PARTIAL This way the buffers can be smaller then the display to save RAM. At least 1/10 screen sized buffer(s) are recommended.
     * LV_DISPLAY_RENDER_MODE_DIRECT The buffer(s) has to be screen sized and LVGL will render into the correct location of the buffer. This way the buffer always contain the whole image. With 2 buffers the buffers’ content are kept in sync automatically. (Old v7 behavior)
     * LV_DISPLAY_RENDER_MODE_FULL Just always redraw the whole screen.
    */
    lv_display_set_buffers(disp, buf1, buf2, DISP_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, (lv_indev_read_cb_t) touch_driver_read);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    create_demo_application();
    /* Force screen refresh */
    lv_refr_now(NULL);

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
    vTaskDelete(NULL);
}


static void file_explorer_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        const char * cur_path =  lv_file_explorer_get_current_path(obj);
        const char * sel_fn = lv_file_explorer_get_selected_file_name(obj);
        
        LV_LOG_USER("%s%s", cur_path, sel_fn);
        printf("CHANGED path: %s file: %s\n", cur_path, sel_fn);
        // Todo move this to a central location
        char * file_open = (char *) malloc(1 + strlen(cur_path)+ strlen(sel_fn) );
        strcpy(file_open, cur_path);
        strcat(file_open, sel_fn);

        if (is_end_with(sel_fn, ".gif") == true) {
            printf("GIF viewer not implemented\n");
        }

        if (is_end_with(sel_fn, ".jpg") || is_end_with(sel_fn, ".JPG")) {
            tab_open_file = lv_tabview_add_tab(tab_main_view, sel_fn);
            lv_tabview_set_active(tab_main_view, lv_tabview_get_tab_count(tab_main_view), LV_ANIM_OFF);
            
            /* Build up the img descriptor of LVGL */
            lv_image_dsc_t imgdsc;
            imgdsc.header.cf = LV_COLOR_FORMAT_RAW;
            imgdsc.data = lv_read_img(file_open, imgdsc);
            imgdsc.header.w = 720;
            imgdsc.header.h = 609;
            ESP_LOG_BUFFER_HEX(TAG, imgdsc.data, 10);
            ESP_LOGI(TAG, "DOES NOT WORK YET!\nimg w:%d h:%d with %d bytes", imgdsc.header.w, imgdsc.header.h, (int) imgdsc.data_size);
            lv_obj_t * wp;

            wp = lv_img_create(tab_open_file);
            lv_image_set_src(wp, &imgdsc);

            lv_obj_set_width(wp, 1000);
            lv_obj_set_height(wp, 700);
        }

        if (is_end_with(sel_fn, ".txt") == true) {
            // Check how to delete when we have more than X tabs
            /* if (file_open_tabs>0) {
              lv_obj_clean(tab_open_file);
            } */
            
            /*Add content to the tabs*/
            tab_open_file = lv_tabview_add_tab(tab_main_view, sel_fn);
        
            // TODO: Check how to set last Tab as active
            lv_tabview_set_active(tab_main_view, lv_tabview_get_tab_count(tab_main_view), LV_ANIM_OFF);
            printf("PATH to open: %s\n\n", file_open);
            const char * file_content = lv_read_file(file_open);
            printf("\n\n%s", file_content);
            /* Create the text area */
            lv_obj_t * ta = lv_textarea_create(tab_open_file);
            lv_obj_set_width(ta, 1000);
            lv_obj_set_height(ta, 700);
            lv_textarea_set_text(ta, file_content);
        }
        
    }
}

void lv_example_file_explorer(lv_obj_t * tab)
{
    fs_init();
    lv_obj_t * file_explorer = lv_file_explorer_create(tab);
    lv_file_explorer_set_sort(file_explorer, LV_EXPLORER_SORT_KIND);
    lv_file_explorer_open_dir(file_explorer, "/S");

    lv_obj_add_event_cb(file_explorer, file_explorer_event_handler, LV_EVENT_ALL, NULL);
}

/***
 * Switch event - updates PWM duty
 **/
static void switch_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t*) lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        LV_UNUSED(obj);
        bool slider_state = lv_obj_has_state(obj, LV_STATE_CHECKED);
        LV_LOG_USER("State: %s\n", slider_state ? "On" : "Off");

        lv_label_set_text(switch_label, slider_state ? "ON" : "OFF");

        int led_duty = (int)slider_state * led_duty_multiplier * 1000;
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

/**
 * A default slider with a label displaying the current value
 */
void create_demo_application(void)
{
    /* Create a Tab view object (global) */
    tab_main_view = lv_tabview_create(lv_scr_act());
    lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF);

    /* Add 2 tabs (the tabs are page (lv_page) and can be scrolled*/
    tab_main = lv_tabview_add_tab(tab_main_view, "SD Explorer");
    tab_settings = lv_tabview_add_tab(tab_main_view, "Settings");
    lv_obj_remove_flag(tab_settings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * sw;
    sw = lv_switch_create(tab_settings);
    lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sw, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    switch_label = lv_label_create(tab_settings);
    lv_label_set_text(switch_label, "LED ON");

    lv_obj_t * cb;
    cb = lv_checkbox_create(tab_settings);
    lv_checkbox_set_text(cb, "ON");

    lv_obj_add_event_cb(sw, switch_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    lv_example_file_explorer(tab_main);
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
