
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#define SDA_PIN  GPIO_NUM_39
#define SCL_PIN  GPIO_NUM_40
#define I2C_PORT I2C_NUM_0

// When the touch panel has different pixels definition
float x_adjust = 1.55;
float y_adjust = 0.8;

esp_err_t i2c_init(void)
{
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 50000
    };
    i2c_param_config(I2C_PORT, &i2c_conf);

    esp_err_t i2c_driver = i2c_driver_install(I2C_PORT, i2c_conf.mode, 0, 0, 0);
	if (i2c_driver == ESP_OK) {
		printf("i2c_driver started correctly\n");
	} else {
		printf("i2c_driver error: %d\n", i2c_driver);
	}

    return ESP_OK;
}
void i2cscan() {
    printf("i2c scan: \n");
     for (uint8_t i = 1; i < 127; i++)
     {
        int ret;
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
        i2c_cmd_link_delete(cmd);
    
        char * device;
        if (ret == ESP_OK)
        {
            switch (i)
            {
            case 0x20:
                device = (char *)"PCA9535";
                break;
            case 0x38:
                device = (char *)"FT6X36 Touch";
                break;
            case 0x51:
                device = (char *)"PCF8563 RTC";
                break;
            case 0x68:
                device = (char *)"DS3231 /TPS PMIC";
                break;
            default:
                device = (char *)"unknown";
                break;
            }
            printf("Found device at: 0x%2x %s\n", i, device);
        }
    }
}
void app_main() {
    printf("GT911 test SDA:%d SCL:%d GPIO_NUM_NC:%d\n",SDA_PIN,SCL_PIN,GPIO_NUM_NC);
    i2c_init();

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_touch_handle_t tp;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1025,
        .y_max = 770,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };
    
    esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_PORT, &tp_io_config, &tp_io_handle);

    esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp);

    
    while (true) {
        esp_lcd_touch_read_data(tp);

        uint16_t touch_x[2];
        uint16_t touch_y[2];
        uint16_t touch_strength[2];
        uint8_t touch_cnt = 0;
        bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 2);
        if (touchpad_pressed) {
            printf("xa:%d ya:%d   x:%d y:%d str:%d count:%d\n\n", 
            (int)(touch_x[0]*x_adjust), (int)(touch_y[0]*y_adjust), (int)touch_x[0], (int)touch_y[0], (int)touch_strength[0], touch_cnt);
        }
    }
}