/*
 * SPDX-FileCopyrightText: 2024 FASANI CORPORATION
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LCD touch: TMA445
 */

#pragma once

#include "esp_lcd_touch.h"
#include <driver/i2c.h>

#define TOUCH_ADDR 0x24

#define CY_REG_BASE         	0x00
#define GET_NUM_TOUCHES(x)     	((x) & 0x0F)
#define GET_TOUCH1_ID(x)       	(((x) & 0xF0) >> 4)
#define GET_TOUCH2_ID(x)       	((x) & 0x0F)
#define CY_XPOS             		0
#define CY_YPOS             		1
#define CY_MT_TCH1_IDX      		0
#define CY_MT_TCH2_IDX      		1
#define be16_to_cpu(x) (uint16_t)((((x) & 0xFF00) >> 8) | (((x) & 0x00FF) << 8))

#define CY_OPERATE_MODE    	0x00
#define CY_SOFT_RESET_MODE  0x01
#define CY_LOW_POWER       	0x04 // Non used for now
#define CY_HNDSHK_BIT       0x80

// I2C common protocol defines
#define WRITE_BIT                          I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                           I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                       0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                      0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                            0x0              /*!< I2C ack value */
#define NACK_VAL                           0x1              /*!< I2C nack value */

// Security KEY
static uint8_t sec_key[] = {0x00, 0xFF, 0xA5, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    
#define DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#define GET_BOOTLOADERMODE(reg)		((reg & 0x10) >> 4)

/* TrueTouch Standard Product Gen3 (Txx3xx) interface definition */
struct cyttsp_xydata {
	uint8_t hst_mode;
	uint8_t tt_mode;
	uint8_t tt_stat;
	uint16_t x1 __attribute__ ((packed));
	uint16_t y1 __attribute__ ((packed));
	uint8_t z1;
	uint8_t touch12_id;
	uint16_t x2 __attribute__ ((packed));
	uint16_t y2 __attribute__ ((packed));
	uint8_t z2;
};

struct cyttsp_bootloader_data {
	uint8_t bl_file;
	uint8_t bl_status;
	uint8_t bl_error;
	uint8_t blver_hi;
	uint8_t blver_lo;
	uint8_t bld_blver_hi;
	uint8_t bld_blver_lo;
	uint8_t ttspver_hi;
	uint8_t ttspver_lo;
	uint8_t appid_hi;
	uint8_t appid_lo;
	uint8_t appver_hi;
	uint8_t appver_lo;
	uint8_t cid_0;
	uint8_t cid_1;
	uint8_t cid_2;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new TMA445 touch driver
 *
 * @note The I2C communication should be initialized before use this function.
 *
 * @param io LCD/Touch panel IO handle
 * @param config: Touch configuration
 * @param out_touch: Touch instance handle
 * @return
 *      - ESP_OK                    on success
 *      - ESP_ERR_NO_MEM            if there is no memory for allocating main structure
 */
esp_err_t esp_lcd_touch_new_i2c_tma445(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch);

/**
 * @brief I2C address of the TMA445 controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_TMA445_ADDRESS (0x24)

/**
 * @brief Touch IO configuration structure
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_TMA445_CONFIG()           \
    {                                       \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_TMA445_ADDRESS, \
        .control_phase_bytes = 1,           \
        .dc_bit_offset = 0,                 \
        .lcd_cmd_bits = 8,                  \
        .flags =                            \
        {                                   \
            .disable_control_phase = 1,     \
        }                                   \
    }

#ifdef __cplusplus
}
#endif
