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
#include "esp_timer.h"

// LVGL
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "SHARP"
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
    printf("app_main started. DISP_BUF_SIZE:%d LV_HOR_RES_MAX:%d V_RES_MAX:%d\n", DISP_BUF_SIZE, LV_HOR_RES_MAX, LV_VER_RES_MAX);
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
    /* Actual size in pixels, not bytes. PLEASE NOTE:
       This size must much the size of DISP_BUF_SIZE declared on lvgl_helpers.h
    */
    uint32_t size_in_px = LV_HOR_RES_MAX*(LV_VER_RES_MAX/10);
    //size_in_px *= 8;


    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    
    // Rounder disabled
    #if defined CONFIG_LV_EPAPER_EPDIY_DISPLAY_CONTROLLER
      //disp_drv.rounder_cb = disp_driver_rounder;
    #endif
    /* When using an epaper display we need to register these additional callbacks */
    disp_drv.flush_cb = disp_driver_flush;
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
    if (lv_switch_get_state(obj)) {
    printf("switch: ON Going to SLEEP!");
    //vTaskDelay(pdMS_TO_TICKS(500));
    //esp_deep_sleep_start();
    } else {
        printf("switch: OFF");
    }
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
        lv_obj_set_pos(obj, obj->coords.x1, obj->coords.y1+20);
        break;
    
    case LV_EVENT_LONG_PRESSED:
        lv_obj_set_pos(obj, obj->coords.x1, obj->coords.y1-20);
        break;
    }
    
}

static void checkbox_handler(lv_obj_t * obj, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        printf("State: %s\n", lv_checkbox_is_checked(obj) ? "Checked" : "Unchecked");
    }
}

static void create_demo_application(void)
{
    // Notes: After first refresh the tabview boxes are loosing their top margin
    //        Also refreshing certain areas is messing the framebuffer (corrupted?)
    tv = lv_tabview_create(lv_scr_act(), NULL);
    lv_obj_set_height(tv,560);

    lv_obj_t *btn = lv_btn_create(tv, NULL);
    // Printing this button 10 pixel y down, refreshed it again to 0,0 in the pixel callback. Why?
    lv_obj_set_pos(btn,  10, 20);
    lv_obj_set_width(btn, lv_obj_get_width_grid(tv, 2, 1));
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Small");
    lv_obj_set_event_cb(btn, btn_cb);

    lv_obj_t *btn2 = lv_btn_create(tv, NULL);
    //             obj , x  , y -> Doing a Y more than 100 it simply get's the down part of the button out
    //                             and prints this button end in the top left (Buf too small?)
    lv_obj_set_pos(btn2, 480, 90);
    lv_obj_set_width(btn2, lv_obj_get_width_grid(tv, 2, 1));
    label2 = lv_label_create(btn2, NULL);
    lv_label_set_text(label2, "BUTTON 2");
    lv_obj_set_event_cb(btn2, btn2_cb);

    lv_obj_t * cb = lv_checkbox_create(tv, NULL);
    lv_checkbox_set_text(cb, "I agree.");
    lv_obj_align(cb, NULL, LV_ALIGN_IN_TOP_LEFT, 30, 70);
    lv_obj_set_event_cb(cb, checkbox_handler);

    /*Create a normal drop down list*/
    lv_obj_t * ddlist = lv_dropdown_create(tv, NULL);
    lv_dropdown_set_options(ddlist, "Apple\n"
            "Banana\n"
            "Orange\n"
            "Melon\n"
            "Grape\n"
            "Raspberry");
    lv_obj_align(ddlist, NULL, LV_ALIGN_IN_TOP_RIGHT, -20, 20);

    /*Create a switch and apply the styles*/
    lv_obj_t *sw1 = lv_switch_create(tv, NULL);
    lv_obj_set_pos(sw1, 30, 110);
    lv_obj_set_event_cb(sw1, btn_sleep_cb);
    lv_obj_t *sw_label = lv_label_create(tv, NULL);
    lv_obj_align(sw_label, sw1, LV_LABEL_ALIGN_CENTER, 0, 28);
    lv_label_set_text(sw_label, "SLEEP");
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
