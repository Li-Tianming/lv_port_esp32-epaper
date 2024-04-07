#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include <esp_log.h>
#include "driver/gpio.h"
#include <driver/i2c.h>

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define SDA_PIN 39
#define SCL_PIN 40
#define TS_INT  3
#define TS_RES  9

#define TOUCH_ADDR 0x24
#define CY_OPERATE_MODE    		0x00
#define CY_SOFT_RESET_MODE      0x01
#define CY_LOW_POWER         	0x04

enum {
	REG_HST_MODE = 0x0,
	REG_MFG_STAT,
	REG_MFG_CMD,
	REG_MFG_REG0,
	REG_MFG_REG1,
	REG_SYS_SCN_TYP = 0x1c,
};

/* Bootloader File 0 offset */
#define CY_BL_FILE0       0x00
/* Bootloader command directive */
#define CY_BL_CMD         0xFF
/* Bootloader Enter Loader mode */
#define CY_BL_ENTER       0x38
/* Bootloader Write a Block */
#define CY_BL_WRITE_BLK   0x39
/* Bootloader Terminate Loader mode */
#define CY_BL_TERMINATE   0x3B
/* Bootloader Exit and Verify Checksum command */
#define CY_BL_EXIT        0xA5

#define CY_HNDSHK_BIT     0x80
/* Bootloader default keys */
#define CY_BL_KEY0 0
#define CY_BL_KEY1 1
#define CY_BL_KEY2 2
#define CY_BL_KEY3 3
#define CY_BL_KEY4 4
#define CY_BL_KEY5 5
#define CY_BL_KEY6 6
#define CY_BL_KEY7 7

// I2C common protocol defines
#define WRITE_BIT                          I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                           I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                       0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                      0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                            0x0              /*!< I2C ack value */
#define NACK_VAL                           0x1              /*!< I2C nack value */

static uint8_t bl_cmd[] = {
	CY_BL_FILE0, CY_BL_CMD, CY_BL_EXIT,
	CY_BL_KEY0, CY_BL_KEY1, CY_BL_KEY2,
	CY_BL_KEY3, CY_BL_KEY4, CY_BL_KEY5,
	CY_BL_KEY6, CY_BL_KEY7
};
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

static const char *LOGG = "LOG";
static const char *TAG = "i2cscanner";

static void i2c_master_init() {
    i2c_config_t conf = { };
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = SDA_PIN;
    conf.scl_io_num = SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

static esp_err_t i2c_master_read_slave_reg(i2c_port_t i2c_num, uint8_t i2c_addr, uint8_t i2c_reg, uint8_t* data_rd, size_t size) {
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    // first, send device address (indicating write) & register to be read
    i2c_master_write_byte(cmd, ( i2c_addr << 1 ), ACK_CHECK_EN);
    // send register we want
    i2c_master_write_byte(cmd, i2c_reg, ACK_CHECK_EN);
    // Send repeated start
    i2c_master_start(cmd);
    // now send device address (indicating read) & read data
    i2c_master_write_byte(cmd, ( i2c_addr << 1 ) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_write_slave_reg(i2c_port_t i2c_num, uint8_t i2c_addr, uint8_t i2c_reg, uint8_t* data_wr, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    // first, send device address (indicating write) & register to be written
    i2c_master_write_byte(cmd, ( i2c_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    // send register we want
    i2c_master_write_byte(cmd, i2c_reg, ACK_CHECK_EN);
    // write the data
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Read contents of a register
---------------------------------------------------------------------------*/
esp_err_t i2c_read_reg( uint8_t reg, uint8_t *pdata, uint8_t count ) {
	return( i2c_master_read_slave_reg( I2C_NUM_0, TOUCH_ADDR,  reg, pdata, count ) );
}

/* Write value to specified register
---------------------------------------------------------------------------*/
esp_err_t i2c_write_reg( uint8_t reg, uint8_t *pdata, uint8_t count ) {
	return( i2c_master_write_slave_reg( I2C_NUM_0, TOUCH_ADDR,  reg, pdata, count ) );
}

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
        }
    }
}

void i2cscanner(void *ignore) {
    while (1) {
        esp_err_t res;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
        printf("00:         ");
        for (uint8_t i = 3; i < 0x78; i++)
        {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, 1 /* expect ack */);
            i2c_master_stop(cmd);
    
            res = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
            if (i % 16 == 0)
                printf("\n%.2x:", i);
            if (res == 0)
                printf(" %.2x", i);
            else
                printf(" --");
            i2c_cmd_link_delete(cmd);
        }
        printf("\n\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void tassss(void *ignore) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void resetTouch() {
    gpio_set_level(TS_RES, 1);
    DELAY(20);
    gpio_set_level(TS_RES, 0);
    DELAY(40);
    gpio_set_level(TS_RES, 1);
    DELAY(20);
}

static int _cyttsp_hndshk_n_write(uint8_t write_back)
{
	int retval = -1;
	uint8_t hst_mode[1];
	uint8_t cmd[1];
	uint8_t tries = 0;
	while (retval < 0 && tries++ < 20){
		DELAY(5);
        retval = i2c_read_reg(REG_HST_MODE, &hst_mode, sizeof(hst_mode));
        if(retval < 0) {	
			printf("%s: bus read fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
			continue;
		}
        cmd[0] = hst_mode[0] & CY_HNDSHK_BIT ?
		write_back & ~CY_HNDSHK_BIT :
		write_back | CY_HNDSHK_BIT;
        retval = i2c_write_reg(0x00, &cmd, sizeof(cmd));
        if(retval < 0)
			printf("%s: bus write fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
    }
    return retval;
}

// 317 cyttsp.c
static int _cyttsp_hndshk()
{
	int retval = -1;
	uint8_t hst_mode[1];
	uint8_t cmd[1];
	uint8_t tries = 0;
	while (retval < 0 && tries++ < 20){
		DELAY(5);
        retval = i2c_read_reg(REG_HST_MODE, &hst_mode, sizeof(hst_mode));
        if(retval < 0) {	
			printf("%s: bus read fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
			continue;
		}
        cmd[0] = hst_mode[0] & CY_HNDSHK_BIT ?
		hst_mode[0] & ~CY_HNDSHK_BIT :
		hst_mode[0] | CY_HNDSHK_BIT;
        retval = i2c_write_reg(0x00, &cmd, sizeof(cmd));
        if(retval < 0)
			printf("%s: bus write fail on handshake ret=%d, retries=%d\n",
				__func__, retval, tries);
    }
    return retval;
}


void touchStuff() {

    // soft reset
    esp_err_t err;
    uint8_t softres[] = {0x01};
    err = i2c_write_reg(0x00, &softres, 1);
    if (err != ESP_OK) {
        printf("i2c_write_reg CY_SOFT_RESET_MODE FAILED \n");
    }
    DELAY(50);
    // write sec_key
    err = i2c_write_reg(0x00, &sec_key, sizeof(sec_key));
    if (err != ESP_OK) {
        printf("i2c_write_reg sec_key FAILED \n");
    }
    printf("i2c_write_reg sec_key OK \n");
    DELAY(88);
    
    // Before this there is a part that reads sysinfo and sets some registers
    // Maybe that is the key to start the engines
    // 959 static int cyttsp_set_sysinfo_mode
    uint8_t tries = 0;
    struct cyttsp_bootloader_data  bl_data = {};
    do {
		DELAY(20);
        i2c_read_reg(0x00, &bl_data, sizeof(bl_data));
        uint8_t *bl_data_p = (uint8_t *)&(bl_data);
        for (int i = 0; i < sizeof(struct cyttsp_bootloader_data); i++) {
			printf("bl_data[%d]=0x%x\n", i, bl_data_p[i]);
        }
        printf("bl_data status=0x%x\n", bl_data.bl_status);
	} while (GET_BOOTLOADERMODE(bl_data.bl_status) && tries++ < 10);

    printf("bl_data status=0x%x\n", GET_BOOTLOADERMODE(bl_data.bl_status));
   
    // Exit BL
    /*     err = i2c_write_reg(0x00, &bl_cmd, sizeof(bl_cmd));
    if (err != ESP_OK) {
        printf("i2c_write_reg bl_cmd FAILED \n");
    }
    printf("i2c_write_reg bl_cmd OK \n"); */

    // Set OP mode
    int retval;
    uint8_t cmd = CY_OPERATE_MODE;

    struct cyttsp_xydata xy_data;
    printf("%s set operational mode\n",__func__);
	memset(&(xy_data), 0, sizeof(xy_data));

    retval = _cyttsp_hndshk_n_write(cmd);
   if (retval < 0) {
		printf("%s: Failed writing block data, err:%d\n",
			__func__, retval);
    }
    _cyttsp_hndshk();
    /* wait for TTSP Device to complete switch to Operational mode */
	DELAY(20);
    retval = i2c_read_reg(0x00, &xy_data, sizeof(xy_data));
    printf("%s: hstmode:0x%x tt_mode:0x%x tt_stat:0x%x ", __func__, xy_data.hst_mode, xy_data.tt_mode, xy_data.tt_stat);
 
}

void app_main()
{
    

    gpio_set_direction(TS_RES, GPIO_MODE_OUTPUT);
    // Not need if is already set in io_conf
    gpio_pullup_en(TS_INT);
    gpio_set_direction(TS_INT, GPIO_MODE_INPUT);
    /*
    // Comment this for now since INT pin should get low first
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = TS_INT;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Setup interrupt for this IO that goes low on the interrupt
    gpio_set_intr_type(TS_INT, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(TS_INT, gpio_isr_handler, (void *)TS_INT);
    */
    i2c_master_init();

    // Start task
    //xTaskCreatePinnedToCore(i2cscanner, TAG, configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);

    resetTouch();
    touchStuff();

    printf("Waiting for INT pin signal\n");

int touched = 1;
while (1) {
    // INT Pin never get's LOW on touch so far
 uint8_t ri = gpio_get_level(TS_INT);
 if (ri == 0) {
    printf("I %d c:%d\n", ri, touched++);
     _cyttsp_hndshk();
    }
 DELAY(10);
   }
}