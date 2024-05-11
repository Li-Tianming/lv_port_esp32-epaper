/* LVGL Example project for Epaper displays
 * Just a simple layout with a slider

 * Optional: When using v7 Kaleido PCB, trigger PWM from Front-Light (FL)
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
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// LVGL
#include "lvgl/lvgl.h"
#include "lvgl_helpers.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "epaper"

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


#define FL_WARM   GPIO_NUM_11
#define FL_COLD   GPIO_NUM_12

#define LV_TICK_PERIOD_MS 1
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (11) // Define the output GPIO: 11 for Kindle
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (0) // 4096 Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

static void slider_event_cb(lv_event_t * e);

static lv_obj_t * slider;
static lv_obj_t * slider_label;
uint8_t led_duty_multiplier = 80;

/**********************
 *   APPLICATION MAIN
 **********************/

void app_main() {
    printf("Epaper example. LVGL version %d.%d\n\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);

    //printf("app_main started. DISP_BUF_SIZE:%d LV_HOR_RES_MAX:%d V_RES_MAX:%d\n", DISP_BUF_SIZE, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    gpio_set_direction(FL_WARM, GPIO_MODE_OUTPUT);
    gpio_set_direction(FL_COLD, GPIO_MODE_OUTPUT);
    gpio_set_level(FL_WARM, 0);
    gpio_set_level(FL_COLD, 0);
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
    printf("DISP_BUF*sizeof(lv_color_t) %d", DISP_BUF_SIZE * sizeof(lv_color_t));

    lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 != NULL);

    // Do not use double buffer for epaper
    lv_color_t* buf2 = NULL;

    static lv_disp_draw_buf_t disp_buf;
    /* Actual size in pixels, not bytes. PLEASE NOTE:
       This size must much the size of DISP_BUF_SIZE declared on lvgl_helpers.h
    */
    uint32_t size_in_px = DISP_BUF_SIZE;
    //size_in_px *= 8;


       /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;
  /*Set the resolution of the display*/
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
    disp_drv.set_px_cb = disp_driver_set_px;
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
#endif
    
    disp_drv.draw_buf = &disp_buf;
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

/***
 * slider event - updates PWM duty
 **/
static void slider_event_cb(lv_event_t * e)
{
    slider = lv_event_get_target(e);
    char buf[8];
    int sliderv = (int)lv_slider_get_value(slider);
    int led_duty = sliderv * led_duty_multiplier;
    printf("v:%d\n",sliderv);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    lv_snprintf(buf, sizeof(buf), "%d%%", sliderv);
    lv_label_set_text(slider_label, buf);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

bool fl_status = false;

static void event_handler_on(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int led_duty = 0;
    
    if(code == LV_EVENT_VALUE_CHANGED) {
        fl_status = !fl_status;
        int duty = (fl_status) ? 90 : 0;
        led_duty = duty * led_duty_multiplier;
        lv_slider_set_value(slider, duty, LV_ANIM_ON);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", duty);
        lv_label_set_text(slider_label, buf);

        //printf("code: %d\n", (int)code);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, led_duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

/**
 * A default slider with a label displaying the current value
 */
void create_demo_application(void)
{
    /*Create a slider in the center of the display*/
    slider = lv_slider_create(lv_scr_act());
    lv_obj_center(slider);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /*Create a label below the slider*/
    slider_label = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_label, "0%");

    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t * label;
    lv_obj_t * btn2 = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(btn2, event_handler_on, LV_EVENT_ALL, NULL);
    lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_height(btn2, LV_SIZE_CONTENT);

    label = lv_label_create(btn2);
    lv_label_set_text(label, "Toggle ON/OFF");
    lv_obj_center(label);
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
