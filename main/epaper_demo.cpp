/* LVGL Example project for Epaper displays
 * Just a simple layout, buttons and checkboxes
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_freertos_hooks.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"

// LVGL
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "epaper"
#define LV_TICK_PERIOD_MS 1

extern "C"
{
    void app_main();
}
/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void create_demo_application(void);

#define TOUCH_INT_GPIO 13
/**********************
 *   APPLICATION MAIN
 **********************/

void app_main() {
    printf("app_main started. Setting up touch awake \n");
   
   // Does never triggers
   switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            /* uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                printf("Wake up from GPIO %d\n", pin);
            } else {
                printf("Wake up from GPIO\n");
            } */
            printf("Wake up from ESP_SLEEP_WAKEUP_EXT1\n");
            break;
        }

        case ESP_SLEEP_WAKEUP_GPIO: {
            /* uint64_t wakeup_pin_mask = esp_sleep_get_gpio_wakeup_status();
            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                printf("Wake up from GPIO %d\n", pin);
            } else {
                printf("Wake up from GPIO\n");
            } */
            printf("Wake up from ESP_SLEEP_WAKEUP_GPIO\n");
            break;
        }

        case ESP_SLEEP_WAKEUP_TIMER: {
            printf("Wake up from timer.\n");
            //printf("Wake up from timer. Time spent in deep sleep: %dms\n", sleep_time_ms);
            break;
        }
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        case ESP_SLEEP_WAKEUP_ALL:
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
        case ESP_SLEEP_WAKEUP_ULP:
        case ESP_SLEEP_WAKEUP_UART:
        case ESP_SLEEP_WAKEUP_WIFI:
        case ESP_SLEEP_WAKEUP_COCPU:
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        case ESP_SLEEP_WAKEUP_BT:
        break;
   }

    esp_err_t ext1wake = esp_sleep_enable_ext1_wakeup(0x2000, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_err_t ext1enable = esp_sleep_enable_gpio_wakeup();
    // Enable timed wakeup
    const int wakeup_time_sec = 20;
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
    esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);

    printf("ext1wake:%d ext1enable:%d\n", ext1wake, ext1enable);
    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, NULL, 0, NULL, 1);
}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();
    // Screen is cleaned in first flush

    lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    // Do not use double buffer for epaper
    lv_color_t* buf2 = NULL;

    static lv_disp_buf_t disp_buf;
    uint32_t size_in_px = DISP_BUF_SIZE;

    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;


    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

    /* When using an epaper display we need to register these additional callbacks */
    #if defined CONFIG_LV_EPAPER_EPDIY_DISPLAY_CONTROLLER
      disp_drv.rounder_cb = disp_driver_rounder;
    #endif
    disp_drv.set_px_cb = disp_driver_set_px;
    
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
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

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * tv;


// Create this global since we will access from a callback function
lv_obj_t * ta1;

uint8_t btn_size = 1;
lv_obj_t * label;lv_obj_t * label2;lv_obj_t * label3;

static void btn_sleep_cb(lv_obj_t * obj, lv_event_t e)
{
    printf("SLEEP x:%d y%d\n\n",obj->coords.x1,obj->coords.y1);
    esp_deep_sleep_start();
}

static void btn_cb(lv_obj_t * obj, lv_event_t e)
{
    printf("click x:%d y%d\n\n",obj->coords.x1,obj->coords.y1);

    btn_size = (btn_size == 1) ? 2 : 1;
    //lv_obj_set_pos(btn, 0, 100);
    lv_obj_set_width(obj, lv_obj_get_width_grid(tv, btn_size, 1));
    lv_label_set_text(label , (btn_size == 1) ? "BIG" : "Small");
}
static void btn2_cb(lv_obj_t * obj, lv_event_t e)
{
    printf("click2 x:%d y%d\n\n",obj->coords.x1,obj->coords.y1);

    switch (e) {
    case LV_EVENT_PRESSED:
        lv_obj_set_pos(obj, 0, obj->coords.y1+20);
        break;
    
    case LV_EVENT_LONG_PRESSED:
        lv_obj_set_pos(obj, 0, obj->coords.y1-20);
        break;
    }
    
}

static void btn3_cb(lv_obj_t * obj, lv_event_t e)
{
    printf("click3 SLEEP x:%d y%d ",obj->coords.x1,obj->coords.y1);

    obj->coords.y1 = obj->coords.y1 +10;
    lv_obj_set_pos(obj, obj->coords.x1, obj->coords.y1);
    
   
    lv_label_set_text(label3 , "SLEEP");
    
    vTaskDelay(1000);

    //esp_light_sleep_start();
    esp_deep_sleep_start();
}

static void create_demo_application(void)
{
    // Notes: After first refresh the tabview boxes are loosing their top margin
    //        Also refreshing certain areas is messing the framebuffer (corrupted?)
    tv = lv_tabview_create(lv_scr_act(), NULL);


    lv_obj_t *btn = lv_btn_create(tv, NULL);
    // Printing this button 10 pixel y down, refreshed it again to 0,0 in the pixel callback. Why?
    lv_obj_set_pos(btn, 0, 10);
    lv_btn_set_fit2(btn, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btn, lv_obj_get_width_grid(tv, 2, 1));
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Small");
    lv_obj_set_event_cb(btn, btn_cb);

    lv_obj_t *btn2 = lv_btn_create(tv, NULL);
    lv_obj_set_pos(btn2, 0, 60);
    lv_btn_set_fit2(btn2, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btn2, lv_obj_get_width_grid(tv, 2, 1));
    label2 = lv_label_create(btn2, NULL);
    lv_label_set_text(label2, "BUTTON 2");
    lv_obj_set_event_cb(btn2, btn2_cb);

    lv_obj_t *btn3 = lv_btn_create(tv, NULL);
    lv_obj_set_pos(btn3, 490, 110);
    lv_btn_set_fit2(btn3, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btn3, lv_obj_get_width_grid(tv, 2, 1));
    label3 = lv_label_create(btn3, NULL);
    lv_label_set_text(label3, "[ SLEEP ]");
    lv_obj_set_event_cb(btn3, btn3_cb);

/* lv_obj_t * btnS = lv_btn_create(tv, NULL);
    lv_btn_set_fit2(btnS, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btnS, lv_obj_get_width_grid(tv, 2, 1));
    label = lv_label_create(btnS, NULL);
    lv_label_set_text(label, "SLEEP");
    lv_obj_set_event_cb(btnS, btn_sleep_cb); */
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
