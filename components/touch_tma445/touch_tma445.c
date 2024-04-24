/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <touch_tma445.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_lcd_touch.h"

static const char *TAG = "TMA445";

/* 7 Header + (Points * 10 data bytes) */
#define ESP_LCD_TOUCH_TMA445_MAX_DATA_LEN (7+CONFIG_ESP_LCD_TOUCH_MAX_POINTS*2) 

/*******************************************************************************
* Function definitions
*******************************************************************************/
static esp_err_t esp_lcd_touch_tma445_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_tma445_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_tma445_del(esp_lcd_touch_handle_t tp);

/* I2C read */
static esp_err_t touch_tma445_i2c_read(esp_lcd_touch_handle_t tp, uint8_t *data, uint8_t len);

/* Added this one since the original touch had a read with -1 as parameter */
static esp_err_t touch_tma445_read_reg(esp_lcd_touch_handle_t tp, uint16_t reg,uint8_t *data, uint8_t len);


/* TMA445 reset */
static esp_err_t touch_tma445_reset(esp_lcd_touch_handle_t tp);

/* TMA445 write */
static esp_err_t touch_tma445_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint16_t len);

static esp_err_t esp_lcd_touch_tma445_enter_sleep(esp_lcd_touch_handle_t tp);

static esp_err_t esp_lcd_touch_tma445_exit_sleep(esp_lcd_touch_handle_t tp);

static int _cyttsp_hndshk(esp_lcd_touch_handle_t tp);


/*******************************************************************************
* Private API function
*******************************************************************************/

/* Reset controller */
static esp_err_t touch_tma445_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

static esp_err_t touch_tma445_i2c_read(esp_lcd_touch_handle_t tp, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, -1, data, len);
}

static esp_err_t touch_tma445_read_reg(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    /* Read data */
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t touch_tma445_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint16_t len)
{
    assert(tp != NULL);
    assert(data != NULL);

    return esp_lcd_panel_io_tx_param(tp->io, reg, data, len);
}

/*******************************************************************************
* Public API functions
*******************************************************************************/

// 317 cyttsp.c
static int _cyttsp_hndshk(esp_lcd_touch_handle_t tp)
{
	int retval = -1;
	uint8_t hst_mode[1];
	uint8_t cmd[1];
	uint8_t tries = 0;
	while (retval < 0 && tries++ < 20){
		DELAY(5);

        retval = touch_tma445_i2c_read(tp, &hst_mode, sizeof(hst_mode));
        if(retval < 0) {
			printf("%s: bus read fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
			continue;
		}
        cmd[0] = hst_mode[0] & CY_HNDSHK_BIT ?
		hst_mode[0] & ~CY_HNDSHK_BIT :
		hst_mode[0] | CY_HNDSHK_BIT;

        retval = touch_tma445_i2c_write(tp, CY_REG_BASE, &cmd, sizeof(cmd));
        if(retval < 0)
			printf("%s: bus write fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
    }
    return retval;
}

esp_err_t esp_lcd_touch_new_i2c_tma445(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;

    assert(config != NULL);
    assert(out_touch != NULL);

    /* Prepare main structure */
    esp_lcd_touch_handle_t esp_lcd_touch_tma445 = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_lcd_touch_tma445, ESP_ERR_NO_MEM, err, TAG, "no mem for TMA445 controller");

    /* Communication interface */
    esp_lcd_touch_tma445->io = io;

    /* Only supported callbacks are set */
    esp_lcd_touch_tma445->read_data = esp_lcd_touch_tma445_read_data;
    esp_lcd_touch_tma445->get_xy = esp_lcd_touch_tma445_get_xy;

    esp_lcd_touch_tma445->del = esp_lcd_touch_tma445_del;
    esp_lcd_touch_tma445->enter_sleep = esp_lcd_touch_tma445_enter_sleep;
    esp_lcd_touch_tma445->exit_sleep = esp_lcd_touch_tma445_exit_sleep;
    /* Mutex */
    esp_lcd_touch_tma445->data.lock.owner = portMUX_FREE_VAL;

    /* Save config */
    memcpy(&esp_lcd_touch_tma445->config, config, sizeof(esp_lcd_touch_config_t));

    /* Prepare pin for touch interrupt */
    if (esp_lcd_touch_tma445->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = true,
            .intr_type = (esp_lcd_touch_tma445->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(esp_lcd_touch_tma445->config.int_gpio_num)
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");

        /* Register interrupt callback */
        if (esp_lcd_touch_tma445->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_tma445, esp_lcd_touch_tma445->config.interrupt_callback);
        }
    }

    /* Prepare pin for touch controller reset */
    if (esp_lcd_touch_tma445->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(esp_lcd_touch_tma445->config.rst_gpio_num)
        };
        ret = gpio_config(&rst_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "GPIO config failed");
    }

    /* Reset controller */
    ret = touch_tma445_reset(esp_lcd_touch_tma445);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "TMA445 reset failed");

    /* Soft RST test */
    uint8_t soft_rst[] = { 0x01 };
    ret = touch_tma445_i2c_write(esp_lcd_touch_tma445, CY_REG_BASE, soft_rst, sizeof(soft_rst));
    if (ret != ESP_OK) {
        printf("i2c_write_reg soft_rst FAILED \n");
    }
    DELAY(50);

    /* Security Key (not so secure) */
    ret = touch_tma445_i2c_write(esp_lcd_touch_tma445, CY_REG_BASE, sec_key, sizeof(sec_key));
    if (ret != ESP_OK) {
        printf("i2c_write_reg sec_key FAILED \n");
    }
    DELAY(88);

    uint8_t tries = 0;
    struct cyttsp_bootloader_data  bl_data = {};
    do {
		DELAY(20);
        touch_tma445_i2c_read(esp_lcd_touch_tma445, &bl_data, sizeof(bl_data));
        //i2c_read_reg(CY_REG_BASE, &bl_data, sizeof(bl_data));
        uint8_t *bl_data_p = (uint8_t *)&(bl_data);
        for (int i = 0; i < sizeof(struct cyttsp_bootloader_data); i++) {
			printf("bl_data[%d]=0x%x\n", i, bl_data_p[i]);
        }
        printf("bl_data status=0x%x\n", bl_data.bl_status);
	} while (GET_BOOTLOADERMODE(bl_data.bl_status) && tries++ < 10);

    printf("bl_data status=0x%x\n", GET_BOOTLOADERMODE(bl_data.bl_status));

    // Set OP mode
    int retval;
    uint8_t cmd = CY_OPERATE_MODE;
    struct cyttsp_xydata xy_data;
    printf("%s set operational mode\n",__func__);
	memset(&(xy_data), 0, sizeof(xy_data));

    /* wait for TTSP Device to complete switch to Operational mode */
	DELAY(20);
    //retval = i2c_read_reg(CY_REG_BASE, &xy_data, sizeof(xy_data));
    touch_tma445_i2c_read(esp_lcd_touch_tma445, &xy_data, sizeof(xy_data));
        
    printf("%s: hstmode:0x%x tt_mode:0x%x tt_stat:0x%x ", __func__, xy_data.hst_mode, xy_data.tt_mode, xy_data.tt_stat);

    // ESP_LOG_BUFFER_HEXDUMP("Read len(rep)", data, sizeof(data), ESP_LOG_INFO);

err:
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error (0x%x)! Touch controller TMA445 initialization failed!", ret);
        if (esp_lcd_touch_tma445) {
            esp_lcd_touch_tma445_del(esp_lcd_touch_tma445);
        }
    }

    *out_touch = esp_lcd_touch_tma445;

    return ret;
}

static esp_err_t esp_lcd_touch_tma445_enter_sleep(esp_lcd_touch_handle_t tp)
{
    uint8_t power_save_cmd[2] = {0x01, 0x08};
    esp_err_t err = touch_tma445_i2c_write(tp, CY_REG_BASE, power_save_cmd, sizeof(power_save_cmd));
    ESP_RETURN_ON_ERROR(err, TAG, "Enter Sleep failed!");

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_tma445_exit_sleep(esp_lcd_touch_handle_t tp)
{
    uint8_t power_save_cmd[2] = {0x00, 0x08};
    esp_err_t err = touch_tma445_i2c_write(tp, CY_REG_BASE, power_save_cmd, sizeof(power_save_cmd));
    ESP_RETURN_ON_ERROR(err, TAG, "Exit Sleep failed!");

    return ESP_OK;
}

static esp_err_t esp_lcd_touch_tma445_read_data(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);
    /* get event data from CYTTSP device */
    int retval;
    struct cyttsp_xydata xy_data;
    retval = touch_tma445_i2c_read(tp, &xy_data, sizeof(xy_data));

    if (retval < 0) {
        printf("%s: Error, fail to read device on host interface bus\n", __func__);
    }
    /* provide flow control handshake */
    _cyttsp_hndshk(tp);
    int touch = GET_NUM_TOUCHES(xy_data.tt_stat);
    
    if (touch) {
        tp->data.points = 1;
        tp->data.coords[0].x = be16_to_cpu(xy_data.x1);
        tp->data.coords[0].y = be16_to_cpu(xy_data.y1);
        tp->data.coords[0].strength = xy_data.z1;
        printf("x1:%d y1:%d z1:%d\n", tp->data.coords[0].x,tp->data.coords[0].y,tp->data.coords[0].strength);
    }
    if (touch == 2) {
        tp->data.points++;
        tp->data.coords[1].x = be16_to_cpu(xy_data.x2);
        tp->data.coords[1].y = be16_to_cpu(xy_data.y2);
        tp->data.coords[1].strength = xy_data.z2;
        //printf("x2:%d y2:%d z2:%d\n", tp->data.coords[1].x,tp->data.coords[1].y,tp->data.coords[1].strength);
    }
    return ESP_OK;
}

static bool esp_lcd_touch_tma445_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);
    int retval;
    struct cyttsp_xydata xy_data;

    retval = touch_tma445_read_reg(tp, CY_REG_BASE, &xy_data, sizeof(xy_data));
    if (retval < 0) {
        printf("%s: Error, fail to read device on host interface bus\n", __func__);
    }
    /* provide flow control handshake */
    _cyttsp_hndshk(tp);
    int touch = GET_NUM_TOUCHES(xy_data.tt_stat);
    
    if (touch) {
        tp->data.points = 1;
        tp->data.coords[0].x = be16_to_cpu(xy_data.x1);
        tp->data.coords[0].y = be16_to_cpu(xy_data.y1);
        printf("x1:%d y1:%d\n", tp->data.coords[0].x, tp->data.coords[0].y);
    }

    /* Invalidate */
    tp->data.points = 0;
    return (*point_num > 0);
}

static esp_err_t esp_lcd_touch_tma445_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    /* Reset GPIO pin settings */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    /* Reset GPIO pin settings */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);

    return ESP_OK;
}
