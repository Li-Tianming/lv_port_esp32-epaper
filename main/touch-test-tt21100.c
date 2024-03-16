
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
//#include "driver/i2c_master.h" // Is not there...
#include "esp_lcd_touch_tt21100.h"
#include "esp_log.h"

#define SDA_PIN  GPIO_NUM_39
#define SCL_PIN  GPIO_NUM_40
// Only for I2C scanner
#define I2C_PORT I2C_NUM_0

#define I2C_MASTER_FREQ_HZ 20000                      /*!< I2C master clock frequency */
#define I2C_SCLK_SRC_FLAG_FOR_NOMAL       (0)         /*!< Any one clock source that is available for the specified frequency may be choosen*/
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */

#define TT21100_CHIP_ADDR_DEFAULT   (0x24)
// TT21100 touch controller needs a Reset pin to be toggled
#define TOUCH_RST_PIN  GPIO_NUM_1
#define TOUCH_INT_PIN  GPIO_NUM_3

esp_err_t i2c_init(void)
{
    const i2c_config_t i2c_conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = SDA_PIN,
    .scl_io_num = SCL_PIN,
    .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    i2c_param_config(I2C_PORT, &i2c_conf);

    esp_err_t i2c_driver = i2c_driver_install(I2C_PORT, i2c_conf.mode,I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (i2c_driver == ESP_OK) {
        printf("i2c_driver started correctly\n");
    } else {
        printf("i2c_driver error: %d\n", i2c_driver);
    }
    
    return ESP_OK;
}

void app_main() {
    printf("TT21100 Touch test SDA:%d SCL:%d GPIO_NUM_NC:%d\n",SDA_PIN,SCL_PIN,GPIO_NUM_NC);

    esp_err_t I2Cbegin = i2c_init();
    if (I2Cbegin == ESP_OK) {
        ESP_LOGI("I2C", "started correctly\n");
    }
    // Setup up touch panel
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_touch_handle_t tp;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1448,
        .y_max = 1072,
        .rst_gpio_num = 1,
        .int_gpio_num = 3,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    
    esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_PORT, &tp_io_config, &tp_io_handle);
    
    esp_lcd_touch_new_i2c_tt21100(tp_io_handle, &tp_cfg, &tp);


    ESP_LOGI("I2C", "Start listening to touch events:\n");

    while (true) {
        int INT = gpio_get_level(TOUCH_INT_PIN);
        esp_lcd_touch_read_data(tp);

        uint16_t touch_x[1];
        uint16_t touch_y[1];
        uint16_t touch_strength[1];
        uint8_t touch_cnt = 0;
        bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 1);
        if (touchpad_pressed) {
            printf("x:%d y:%d str:%d count:%d\n\n", 
            (int)touch_x[0], (int)touch_y[0], (int)touch_strength[0], touch_cnt);
        }

        if (INT == 0) {
            printf("INT: %d", INT);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}