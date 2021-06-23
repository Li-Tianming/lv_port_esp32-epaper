/* LVGL Example project for Epaper displays
 * Just a simple layout, buttons and checkboxes
 */
#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

// LVGL
#include "lvgl_helpers.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
/******************************************
 *      DEFINES: Add your WiFi credentials
 ******************************************/
#define CONFIG_ESP_WIFI_SSID "WLAN-724300"
#define CONFIG_ESP_WIFI_PASSWORD "50238634630558382093"

#define TAG "epaper"
#define LV_TICK_PERIOD_MS 1
#define HTTP_RECEIVE_BUFFER_SIZE 100

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define CONFIG_ESP_MAXIMUM_RETRY 5
char espIpAddress[16];
static int s_retry_num = 0;
uint16_t countDataEventCalls=0;

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

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Connect to the AP failed %d times. Going to deepsleep 10 minutes", CONFIG_ESP_MAXIMUM_RETRY);
            //deepsleep();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(espIpAddress,  IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s\n", espIpAddress);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    sprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), CONFIG_ESP_WIFI_SSID);
    sprintf(reinterpret_cast<char *>(wifi_config.sta.password), CONFIG_ESP_WIFI_PASSWORD);
    wifi_config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

/**********************
 *   APPLICATION MAIN
 **********************/

void app_main() {
    printf("app_main RGB Slider\n");
    //Initialize NVS: WiFi needs this
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect to wifi
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

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

    lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 != NULL);

    // Do not use double buffer for epaper
    lv_color_t* buf2 = NULL;

    static lv_disp_buf_t disp_buf;
    uint32_t size_in_px = DISP_BUF_SIZE;

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

lv_obj_t * slider;
lv_obj_t * r_slider_value;
lv_obj_t * g_slider_value;
lv_obj_t * b_slider_value;

static void slider_red_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(r_slider_value, buf);
    }
}
static void slider_green_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(g_slider_value, buf);
    }
}
static void slider_blue_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(b_slider_value, buf);
    }
}

void create_slider(lv_obj_t * parent_obj, const char *label_text, lv_event_cb_t slider_event, int16_t y_ofs)
{
  /* Create a slider in the center of the display */
    printf("Drawing %s slider y_ofs:%d\n", label_text, y_ofs);

    slider = lv_slider_create(parent_obj, NULL);
    lv_obj_set_width(slider, LV_HOR_RES_MAX/2);
    lv_obj_align(slider, NULL, LV_ALIGN_CENTER, 0, y_ofs);
    lv_obj_set_event_cb(slider, slider_event);
    lv_slider_set_range(slider, 0, 255);
    
    /* Create a label below the slider */
    lv_obj_t * label = lv_label_create(parent_obj, NULL);
    if (strcmp(label_text ,"RED") == 0) {
        lv_label_set_text(label, label_text);
        lv_obj_align(label, slider, LV_ALIGN_OUT_TOP_MID, 0, y_ofs-20);

        r_slider_value = lv_label_create(parent_obj, NULL);
        lv_label_set_text(r_slider_value, "0");
        lv_obj_set_auto_realign(r_slider_value, true);
        lv_obj_align(r_slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, y_ofs+10);
    }

    if (strcmp(label_text ,"GREEN") == 0) {
        lv_label_set_text(label, label_text);
        lv_obj_align(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

        g_slider_value = lv_label_create(parent_obj, NULL);
        lv_label_set_text(g_slider_value, "0");
        lv_obj_set_auto_realign(g_slider_value, true);
        lv_obj_align(g_slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, y_ofs-70);
    }

    if (strcmp(label_text ,"BLUE") == 0) {
        lv_label_set_text(label, label_text);
        lv_obj_align(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -10);

        b_slider_value = lv_label_create(parent_obj, NULL);
        lv_label_set_text(b_slider_value, "0");
        lv_obj_set_auto_realign(b_slider_value, true);
        lv_obj_align(b_slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, y_ofs-142);
    }
}

static void create_demo_application(void)
{

    tv = lv_tabview_create(lv_scr_act(), NULL);
    lv_obj_t * info = lv_label_create(tv, NULL);
    lv_label_set_text(info, "Welcome to the colors slider demo");
    lv_obj_align(info, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);

    // Build UX functions to avoid repeating same code X times
    create_slider(tv, "RED", slider_red_event_cb, 0);
    
    create_slider(tv, "GREEN", slider_green_event_cb, 80);
    
    create_slider(tv, "BLUE", slider_blue_event_cb, 160);
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
