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
        vTaskDelay(pdMS_TO_TICKS(5));

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
static lv_obj_t * t1;
static lv_obj_t * t2;
static lv_obj_t * t3;

static lv_style_t style_box;

// Create this global since we will access from a callback function
lv_obj_t * ta1;

static void btn_sleep_cb(lv_obj_t * obj, lv_event_t e)
{
    printf("Sleep! click x:%d y%d\n\n",obj->coords.x1,obj->coords.y1);
    lv_textarea_set_text(ta1, "SLEEP\n");
    //esp_deep_sleep_start();
}

static void btn2_cb(lv_obj_t * obj, lv_event_t e)
{
    const char  * txt = "Click\n";
        static uint16_t i = 0;
        if(txt[i] != '\0') {
            lv_textarea_add_char(ta1, txt[i]);
            i++;
        }
    printf("button click x:%d y%d\n\n",obj->coords.x1,obj->coords.y1);
}

static void check_cb(lv_obj_t * obj, lv_event_t event)
{
    const char  * txt = lv_checkbox_is_checked(obj) ? "Checked" : "Unchecked";
    if(event == LV_EVENT_VALUE_CHANGED) {
        printf("State: %s\n", txt);
        lv_textarea_set_text(ta1, txt);
    }
}



static void create_demo_application(void)
{
    // Notes: After first refresh the tabview boxes are loosing their top margin
    //        Also refreshing certain areas is messing the framebuffer (corrupted?)
    tv = lv_tabview_create(lv_scr_act(), NULL);

    t1 = lv_tabview_add_tab(tv, "Controls");
    t2 = lv_tabview_add_tab(tv, "Buttons");
    t3 = lv_tabview_add_tab(tv, "Checkbox");

    lv_obj_set_event_cb(t2, btn2_cb);
    lv_obj_set_event_cb(t3, btn2_cb);


    lv_style_init(&style_box);
    lv_style_set_value_align(&style_box, LV_STATE_DEFAULT, LV_ALIGN_OUT_TOP_LEFT);
    lv_style_set_value_ofs_y(&style_box, LV_STATE_DEFAULT, - LV_DPX(30));
    lv_style_set_margin_top(&style_box, LV_STATE_DEFAULT, LV_DPX(30));

    lv_page_set_scrl_layout(t1, LV_LAYOUT_PRETTY_TOP);

    lv_disp_size_t disp_size = lv_disp_get_size_category(NULL);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, disp_size <= LV_DISP_SIZE_SMALL ? 1 : 2, 1);

    lv_obj_t * h = lv_cont_create(t1, NULL);
    lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
    lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_box);
    lv_obj_set_drag_parent(h, true);

    lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Basics");

    lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(h, grid_w);
    lv_obj_t * btn = lv_btn_create(h, NULL);
    lv_btn_set_fit2(btn, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(btn, lv_obj_get_width_grid(h, disp_size <= LV_DISP_SIZE_SMALL ? 1 : 2, 1));
    lv_obj_t * label = lv_label_create(btn, NULL);
    lv_label_set_text(label ,"SLEEP");
    lv_obj_set_event_cb(btn, btn_sleep_cb);

    btn = lv_btn_create(h, btn);
    lv_btn_toggle(btn);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label ,"Button 2");

    ta1 = lv_textarea_create(t1, NULL);
    lv_obj_set_size(ta1, 200, 100);
    lv_obj_align(ta1, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_textarea_set_text(ta1, "text");

    //lv_switch_create(h, NULL);
    //lv_checkbox_create(h, NULL);

    // Create simple button in Tab 2: Buttons
    lv_page_set_scrl_layout(t2, LV_LAYOUT_PRETTY_TOP);
    lv_obj_t * h2 = lv_cont_create(t2, NULL);
    lv_cont_set_layout(h2, LV_LAYOUT_PRETTY_MID);
    lv_obj_add_style(h2, LV_CONT_PART_MAIN, &style_box);
    lv_obj_set_drag_parent(h2, true);

    lv_cont_set_fit2(h2, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(h2, grid_w);
    lv_obj_t * btn2 = lv_btn_create(h2, NULL);
    lv_btn_set_fit2(btn2, LV_FIT_NONE, LV_FIT_NONE);
    lv_obj_set_width(btn2, lv_obj_get_width_grid(h2, 2, 1));
    lv_obj_t * label2 = lv_label_create(btn2, NULL);
    lv_label_set_text(label2 ,"Button Test long");
    lv_obj_set_event_cb(btn2, btn2_cb);
    
    // Create simple Checkbox in Tab 3
    lv_page_set_scrl_layout(t3, LV_LAYOUT_PRETTY_TOP);
    lv_obj_t * h3 = lv_cont_create(t3, NULL);
    lv_cont_set_layout(h3, LV_LAYOUT_PRETTY_MID);
    lv_obj_add_style(h3, LV_CONT_PART_MAIN, &style_box);
    lv_obj_set_drag_parent(h3, true);

    lv_cont_set_fit2(h3, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(h3, grid_w);
    lv_obj_t * btn3 = lv_btn_create(h3, NULL);
    lv_btn_set_fit2(btn3, LV_FIT_NONE, LV_FIT_NONE);
    lv_obj_set_width(btn3, lv_obj_get_width_grid(h3, 2, 1));
    lv_obj_t * label3 = lv_label_create(btn3, NULL);
    lv_label_set_text(label3 ,"Button 3");

    // Trying to uncheck the box does not refresh the right part of the screen
    lv_obj_t * cb = lv_checkbox_create(h3, NULL);
    lv_checkbox_set_state(cb, true);
    lv_checkbox_set_text(cb, "I do not agree with all this Terms");
    lv_obj_set_event_cb(cb, check_cb);
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
