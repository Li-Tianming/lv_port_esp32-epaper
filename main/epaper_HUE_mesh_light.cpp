/* LVGL Example project for Epaper displays
 * Just a simple layout, buttons and checkboxes
 *
 * PREREQUISITES:
 * Fill out your WiFi SSID / PASSWORD below and the MESH MAC address of the lamp you want to control
 * For that in the ESP-MESH mobile app, just press over a lamp and select: About device
 * Copy that MESH MAC address into the define: MESH_LAMP_MAC (You can add more than 1 lamp separating with commas)
 * Alternative is to make a mesh_info query: https://docs.espressif.com/projects/esp-mdf/en/latest/api-guides/mlink.html#app-acquires-the-device-list
 *
 * curl -v 'http://esp32_mesh.local/mesh_info'
 * 
 * Obvious important notice: The lights should be powered on otherwise won't respond to your requests
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
#include "mdns.h"

// LVGL
#include "lvgl_helpers.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
/******************************************
 *      DEFINES: Add your WiFi credentials and MESH_LAMP_IP target
 ******************************************/
#define CONFIG_ESP_WIFI_SSID     "WLAN-724300"
#define CONFIG_ESP_WIFI_PASSWORD "50238634630558382093"
// Leave commented to detect MESH Mac lamps automatically. Fill to target this node(s) comma separated
//#define MESH_LAMP_MAC            "3c71bf9d6ab4,3c71bf9d6980"
#define TAG "epaper"
#define LV_TICK_PERIOD_MS 10
#define HTTP_RECEIVE_BUFFER_SIZE 50
// Leave DEBUG_ENABLED on first tries
//#define DEBUG_ENABLED
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define CONFIG_ESP_MAXIMUM_RETRY 5
char espIpAddress[16];
char meshLampMacs[160];

// Note: If automatic MDNS detection does not work espRootLampURL has to be filled with the URL to query the lamp
//   Ex: http://IP/device_request
//   To  find the IP of your root lamp:  ping esp32_mesh.local
char espRootLampURL[40];
char espRootLampMeshInfoURL[40];
static int s_retry_num = 0;
uint16_t countDataEventCalls = 0;
const char * root_lamp_host = "esp32_mesh";

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

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "ON_HEADER k=%s v=%s\n", evt->header_key, evt->header_value);
            if (strcmp(evt->header_key, "Mesh-Node-Mac") == 0) {
                #ifndef defined MESH_LAMP_MAC
                sprintf(meshLampMacs, "%s", evt->header_value);
                #endif
            }
            break;
        case HTTP_EVENT_ON_DATA:
            countDataEventCalls++;
            #ifdef DEBUG_ENABLED
               ESP_LOGI(TAG, "ON_DATA, len=%d", evt->data_len);
               printf("calls:%d %.*s\n", countDataEventCalls, evt->data_len, (char*)evt->data);
            #endif
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void resolve_mdns_host(const char * host_name)
{
    printf("Query A: %s.local\n", host_name);
    struct ip4_addr addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(host_name, 2000,  (esp_ip4_addr_t*)&addr);

    if(err){
        if(err == ESP_ERR_NOT_FOUND){
            printf("%s.local was not found.\n", host_name);
            return;
        }
        
        printf("MDNS Query Failed. ERR %s. Please resolve the address from your local PC with: ping %s.local\n", esp_err_to_name(err), host_name);
        return;
    }
    
    sprintf(espRootLampURL, "http://"IPSTR"/device_request", IP2STR(&addr));
    sprintf(espRootLampMeshInfoURL, "http://"IPSTR"/mesh_info", IP2STR(&addr));

    printf("Root lamp request URL: %s\n", espRootLampURL);

    #ifndef MESH_LAMP_MAC
    printf("Root lamp requesting Nodes: %s\n", espRootLampMeshInfoURL);
    // Query root lamp for Nodes
    esp_http_client_config_t config = {
        .url = espRootLampMeshInfoURL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 2500,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t errh = esp_http_client_perform(client);
    if (errh == ESP_OK) {
        ESP_LOGI(TAG, "mesh_info CALL Status=%d, content_length=%d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    }else{
        ESP_LOGE(TAG, "\nGET failed:%s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    #endif
}

char *randstring(size_t length) {

    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.-#'?!";        
    char *randomString = NULL;

    if (length) {
        randomString = (char*)malloc(sizeof(char) * (length +1));

        if (randomString) {            
            for (int n = 0;n < length;n++) {            
                int key = rand() % (int)(sizeof(charset) -1);
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }

    return randomString;
}

esp_err_t light_request(uint8_t cid, uint16_t value) {
    char json_request[100];

    esp_http_client_config_t config = {
        .url = espRootLampURL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 1500,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Build POST   
    sprintf(json_request, "{\"request\":\"set_status\",\"characteristics\":[{\"cid\":%d,\"value\":%d}]}",
            cid, value);
    
    esp_http_client_set_header(client, "Mesh-Node-Mac", meshLampMacs);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Connection", "keep-alive");
    esp_http_client_set_header(client, "Randomtoken", randstring(12));
    
    esp_http_client_set_post_field(client, json_request, strlen(json_request));
    // Send the post request
    esp_err_t err = esp_http_client_perform(client);
    // DEBUG Post data and check there are no memory leaks
    //printf("POST(%d) %s \nHeap:%d\n", strlen(json_request), json_request, xPortGetFreeHeapSize());

    if (err == ESP_OK) {
        // Disable after debugging
        ESP_LOGI(TAG, "Status=%d, content_length=%d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    }else{
        ESP_LOGE(TAG, "\nPOST failed:%s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
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
            ESP_LOGI(TAG, "Connect to the AP failed %d times. Restart to try again", CONFIG_ESP_MAXIMUM_RETRY);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(espIpAddress,  IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "\nGot IP: %s\n", espIpAddress);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Initialize MDNS service
        ESP_ERROR_CHECK( mdns_init() );
        printf("MESH root lamp query to find IP\n");
        resolve_mdns_host(root_lamp_host);
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
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
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
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
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
    printf("HUE slider for ESP-MDF Light\n");
    
    #ifdef MESH_LAMP_MAC
      printf("MESH_LAMP_MAC set by define to: %s\n", MESH_LAMP_MAC);
      sprintf(meshLampMacs, "%s", MESH_LAMP_MAC);
    #endif
    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    //Initialize NVS: WiFi needs this
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect to wifi
    wifi_init_sta();

    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, NULL, 0, NULL, 1);

    #ifdef DEBUG_ENABLED
      ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
      printf("Free heap:%d\n",xPortGetFreeHeapSize());
    #endif
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
    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

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
 *  Create this global since we will access from a callback function
 **********************/
static lv_obj_t * tv;
lv_obj_t * slider;
lv_obj_t * r_slider_value;
lv_obj_t * g_slider_value;
lv_obj_t * b_slider_value;

static void slider_hue_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(r_slider_value, buf);
        light_request(1, lv_slider_get_value(slider));
    }
}
static void slider_bright_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(g_slider_value, buf);
        light_request(3, lv_slider_get_value(slider));
    }
}
static void slider_white_event_cb(lv_obj_t * slider, lv_event_t event)
{
    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; /* max 3 bytes for number plus 1 null terminating byte */
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        lv_label_set_text(b_slider_value, buf);
        light_request(5, lv_slider_get_value(slider));
    }
}

void create_slider(lv_obj_t * parent_obj, const char *label_text, lv_event_cb_t slider_event, int16_t y_ofs, uint16_t range)
{
  /* Create a slider in the center of the display */
    printf("Drawing %s slider y_ofs:%d\n", label_text, y_ofs);

    slider = lv_slider_create(parent_obj, NULL);
    lv_obj_set_width(slider, LV_HOR_RES_MAX/2);
    lv_obj_align(slider, NULL, LV_ALIGN_CENTER, 0, y_ofs);
    lv_obj_set_event_cb(slider, slider_event);
    lv_slider_set_range(slider, 0, range);
    
    /* Create a label below the slider */
    lv_obj_t * label = lv_label_create(parent_obj, NULL);
    if (strcmp(label_text ,"HUE") == 0) {
        lv_label_set_text(label, label_text);
        lv_obj_align(label, slider, LV_ALIGN_OUT_TOP_MID, 0, y_ofs-20);

        r_slider_value = lv_label_create(parent_obj, NULL);
        lv_label_set_text(r_slider_value, "0");
        lv_obj_set_auto_realign(r_slider_value, true);
        lv_obj_align(r_slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, y_ofs+10);
    }

    if (strcmp(label_text ,"BRIGHT") == 0) {
        lv_label_set_text(label, label_text);
        lv_obj_align(label, slider, LV_ALIGN_OUT_TOP_MID, 0, -15);

        g_slider_value = lv_label_create(parent_obj, NULL);
        lv_label_set_text(g_slider_value, "0");
        lv_obj_set_auto_realign(g_slider_value, true);
        lv_obj_align(g_slider_value, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, y_ofs-70);
    }

    if (strcmp(label_text ,"WHITE") == 0) {
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
    lv_label_set_text(info, "Welcome to the HUE slider demo to control ESP-MESH Lamps");
    lv_obj_align(info, NULL, LV_ALIGN_IN_TOP_LEFT, 20, 10);

    // Build UX functions to avoid repeating same code X times
    create_slider(tv, "HUE", slider_hue_event_cb, 0, 359);
    
    create_slider(tv, "BRIGHT", slider_bright_event_cb, 80, 100);
    
    create_slider(tv, "WHITE", slider_white_event_cb, 160, 100);
}

static void lv_tick_task(void *arg) {
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}
