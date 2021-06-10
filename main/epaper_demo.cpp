/* LVGL Example project for Epaper displays
 * Just a simple layout, buttons and checkboxes
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
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

/**********************
 *   APPLICATION MAIN
 **********************/

void app_main() {
    printf("app_main started\n");
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
lv_obj_t * btn;
uint8_t btn_size = 1;
lv_obj_t * label;

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
    lv_obj_set_width(btn, lv_obj_get_width_grid(tv, btn_size, 1));
    lv_label_set_text(label , (btn_size == 1) ? "BIG" : "Small");
}

static void create_demo_application(void)
{
    // Notes: After first refresh the tabview boxes are loosing their top margin
    //        Also refreshing certain areas is messing the framebuffer (corrupted?)
    tv = lv_tabview_create(lv_scr_act(), NULL);


    btn = lv_btn_create(tv, NULL);
    // Printing this button 10 pixel y down, refreshed it again to 0,0 in the pixel callback. Why?
    lv_obj_set_pos(btn, 0, 10);
    lv_btn_set_fit2(btn, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btn, lv_obj_get_width_grid(tv, 2, 1));
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Small");
    lv_obj_set_event_cb(btn, btn_cb);

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
