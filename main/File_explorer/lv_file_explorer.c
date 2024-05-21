#include "include/lv_file_explorer.h"
#include "lvgl.h"
#include "core/lv_global.h"
#include <dirent.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_file_explorer_class)

#define FILE_EXPLORER_QUICK_ACCESS_AREA_WIDTH       (22)
#define FILE_EXPLORER_BROWSER_AREA_WIDTH            (100 - FILE_EXPLORER_QUICK_ACCESS_AREA_WIDTH)

#define quick_access_list_button_style (LV_GLOBAL_DEFAULT()->fe_list_button_style)

/**********************
 *      GLOBALS
 **********************/
struct dirent *dp;
DIR *dir;
const bool format_if_mount_failed = false;
static const char * TAG = "Explorer";
/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_file_explorer_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

static void browser_file_event_handler(lv_event_t * e);
#if LV_FILE_EXPLORER_QUICK_ACCESS
    static void quick_access_event_handler(lv_event_t * e);
    static void quick_access_area_event_handler(lv_event_t * e);
#endif

static void init_style(lv_obj_t * obj);
static void show_dir(lv_obj_t * obj, const char * path);
static void strip_ext(char * dir);
static void file_explorer_sort(lv_obj_t * obj);
static void sort_by_file_kind(lv_obj_t * tb, int16_t lo, int16_t hi);
static void exch_table_item(lv_obj_t * tb, int16_t i, int16_t j);
static bool is_end_with(const char * str1, const char * str2);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_file_explorer_class = {
    .base_class     = &lv_obj_class,
    .constructor_cb = lv_file_explorer_constructor,
    .name = "file-explorer",
    .width_def      = LV_SIZE_CONTENT,
    .height_def     = LV_SIZE_CONTENT,
    .instance_size  = sizeof(lv_file_explorer_t)
    
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
/*Initialize your Storage device and File system.*/
static void fs_init(void)
{
    /*E.g. for FatFS initialize the SD card and FatFS itself*/
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    ESP_LOGI(TAG, "Using SDMMC peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set bus width to use:
#if CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.width = 4;
#else
    slot_config.width = 1;
#endif

    // On chips where the GPIOs used for SD card can be configured, set them in
    // the slot_config structure:
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = CONFIG_SD_CLK;
    slot_config.cmd = CONFIG_SD_CMD;
    slot_config.d0 = CONFIG_SD_D0;
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.d1 = CONFIG_SD_D1;
    slot_config.d2 = CONFIG_SD_D2;
    slot_config.d3 = CONFIG_SD_D3;
#endif  // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
#endif

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted in %s", MOUNT_POINT);

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
}

lv_obj_t * lv_file_explorer_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

/*=====================
 * Setter functions
 *====================*/
#if LV_FILE_EXPLORER_QUICK_ACCESS
void lv_file_explorer_set_quick_access_path(lv_obj_t * obj, lv_file_explorer_dir_t dir, const char * path)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    /*If path is unavailable */
    if((path == NULL) || (lv_strlen(path) <= 0)) return;

    char ** dir_str = NULL;
    switch(dir) {
        case LV_EXPLORER_HOME_DIR:
            dir_str = &(explorer->home_dir);
            break;
        case LV_EXPLORER_MUSIC_DIR:
            dir_str = &(explorer->music_dir);
            break;
        case LV_EXPLORER_PICTURES_DIR:
            dir_str = &(explorer->pictures_dir);
            break;
        case LV_EXPLORER_VIDEO_DIR:
            dir_str = &(explorer->video_dir);
            break;
        case LV_EXPLORER_DOCS_DIR:
            dir_str = &(explorer->docs_dir);
            break;
        case LV_EXPLORER_FS_DIR:
            dir_str = &(explorer->fs_dir);
            break;

        default:
            return;
            break;
    }

    /*Free the old text*/
    if(*dir_str != NULL) {
        lv_free(*dir_str);
        *dir_str = NULL;
    }

    /*Allocate space for the new text*/
    *dir_str = lv_strdup(path);
}

#endif

void lv_file_explorer_set_sort(lv_obj_t * obj, lv_file_explorer_sort_t sort)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    explorer->sort = sort;

    file_explorer_sort(obj);
}

/*=====================
 * Getter functions
 *====================*/
const char * lv_file_explorer_get_selected_file_name(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->sel_fn;
}

const char * lv_file_explorer_get_current_path(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->current_path;
}

lv_obj_t * lv_file_explorer_get_file_table(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->file_table;
}

lv_obj_t * lv_file_explorer_get_header(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->head_area;
}

lv_obj_t * lv_file_explorer_get_path_label(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->path_label;
}

#if LV_FILE_EXPLORER_QUICK_ACCESS
lv_obj_t * lv_file_explorer_get_quick_access_area(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->quick_access_area;
}

lv_obj_t * lv_file_explorer_get_places_list(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->list_places;
}

lv_obj_t * lv_file_explorer_get_device_list(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->list_device;
}

#endif

lv_file_explorer_sort_t lv_file_explorer_get_sort(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    return explorer->sort;
}

/*=====================
 * Other functions
 *====================*/
void lv_file_explorer_open_dir(lv_obj_t * obj, const char * dir)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    show_dir(obj, dir);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void lv_file_explorer_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

#if LV_FILE_EXPLORER_QUICK_ACCESS
    explorer->home_dir = NULL;
    explorer->video_dir = NULL;
    explorer->pictures_dir = NULL;
    explorer->music_dir = NULL;
    explorer->docs_dir = NULL;
    explorer->fs_dir = NULL;
#endif

    explorer->sort = LV_EXPLORER_SORT_NONE;

    lv_memzero(explorer->current_path, sizeof(explorer->current_path));

    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);

    explorer->cont = lv_obj_create(obj);
    lv_obj_set_width(explorer->cont, LV_PCT(100));
    lv_obj_set_flex_grow(explorer->cont, 1);

#if LV_FILE_EXPLORER_QUICK_ACCESS
    /*Quick access bar area on the left*/
    explorer->quick_access_area = lv_obj_create(explorer->cont);
    lv_obj_set_size(explorer->quick_access_area, LV_PCT(FILE_EXPLORER_QUICK_ACCESS_AREA_WIDTH), LV_PCT(100));
    lv_obj_set_flex_flow(explorer->quick_access_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(explorer->quick_access_area, quick_access_area_event_handler, LV_EVENT_ALL,
                        explorer);
#endif

    /*File table area on the right*/
    explorer->browser_area = lv_obj_create(explorer->cont);
#if LV_FILE_EXPLORER_QUICK_ACCESS
    lv_obj_set_size(explorer->browser_area, LV_PCT(FILE_EXPLORER_BROWSER_AREA_WIDTH), LV_PCT(100));
#else
    lv_obj_set_size(explorer->browser_area, LV_PCT(100), LV_PCT(100));
#endif
    lv_obj_set_flex_flow(explorer->browser_area, LV_FLEX_FLOW_COLUMN);

    /*The area displayed above the file browse list(head)*/
    explorer->head_area = lv_obj_create(explorer->browser_area);
    lv_obj_set_size(explorer->head_area, LV_PCT(100), LV_PCT(14));
    lv_obj_remove_flag(explorer->head_area, LV_OBJ_FLAG_SCROLLABLE);

#if LV_FILE_EXPLORER_QUICK_ACCESS
    /*Two lists of quick access bar*/
    lv_obj_t * btn;
    /*list 1*/
    explorer->list_device = lv_list_create(explorer->quick_access_area);
    lv_obj_set_size(explorer->list_device, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(lv_list_add_text(explorer->list_device, "DEVICE"), lv_palette_main(LV_PALETTE_ORANGE), 0);

    btn = lv_list_add_button(explorer->list_device, NULL, LV_SYMBOL_DRIVE " File System");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);

    /*list 2*/
    explorer->list_places = lv_list_create(explorer->quick_access_area);
    lv_obj_set_size(explorer->list_places, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(lv_list_add_text(explorer->list_places, "PLACES"), lv_palette_main(LV_PALETTE_LIME), 0);

    btn = lv_list_add_button(explorer->list_places, NULL, LV_SYMBOL_HOME " HOME");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);
    btn = lv_list_add_button(explorer->list_places, NULL, LV_SYMBOL_VIDEO " Video");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);
    btn = lv_list_add_button(explorer->list_places, NULL, LV_SYMBOL_IMAGE " Pictures");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);
    btn = lv_list_add_button(explorer->list_places, NULL, LV_SYMBOL_AUDIO " Music");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);
    btn = lv_list_add_button(explorer->list_places, NULL, LV_SYMBOL_FILE "  Documents");
    lv_obj_add_event_cb(btn, quick_access_event_handler, LV_EVENT_CLICKED, obj);
#endif

    /*Show current path*/
    explorer->path_label = lv_label_create(explorer->head_area);
    lv_label_set_text(explorer->path_label, LV_SYMBOL_EYE_OPEN"https://lvgl.io");
    lv_obj_center(explorer->path_label);

    /*Table showing the contents of the table of contents*/
    explorer->file_table = lv_table_create(explorer->browser_area);
    lv_obj_set_size(explorer->file_table, LV_PCT(100), LV_PCT(86));
    lv_table_set_column_width(explorer->file_table, 0, LV_PCT(100));
    lv_table_set_column_count(explorer->file_table, 1);
    lv_obj_add_event_cb(explorer->file_table, browser_file_event_handler, LV_EVENT_ALL, obj);

    /*only scroll up and down*/
    lv_obj_set_scroll_dir(explorer->file_table, LV_DIR_TOP | LV_DIR_BOTTOM);

    /*Initialize style*/
    init_style(obj);

    LV_TRACE_OBJ_CREATE("finished");
}

static void init_style(lv_obj_t * obj)
{
    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    /*lv_file_explorer obj style*/
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xf2f1f6), 0);

    /*main container style*/
    lv_obj_set_style_radius(explorer->cont, 0, 0);
    lv_obj_set_style_bg_opa(explorer->cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(explorer->cont, 0, 0);
    lv_obj_set_style_outline_width(explorer->cont, 0, 0);
    lv_obj_set_style_pad_column(explorer->cont, 0, 0);
    lv_obj_set_style_pad_row(explorer->cont, 0, 0);
    lv_obj_set_style_flex_flow(explorer->cont, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_pad_all(explorer->cont, 0, 0);
    lv_obj_set_style_layout(explorer->cont, LV_LAYOUT_FLEX, 0);

    /*head cont style*/
    lv_obj_set_style_radius(explorer->head_area, 0, 0);
    lv_obj_set_style_border_width(explorer->head_area, 0, 0);
    lv_obj_set_style_pad_top(explorer->head_area, 0, 0);

#if LV_FILE_EXPLORER_QUICK_ACCESS
    /*Quick access bar container style*/
    lv_obj_set_style_pad_all(explorer->quick_access_area, 0, 0);
    lv_obj_set_style_pad_row(explorer->quick_access_area, 20, 0);
    lv_obj_set_style_radius(explorer->quick_access_area, 0, 0);
    lv_obj_set_style_border_width(explorer->quick_access_area, 1, 0);
    lv_obj_set_style_outline_width(explorer->quick_access_area, 0, 0);
    lv_obj_set_style_bg_color(explorer->quick_access_area, lv_color_hex(0xf2f1f6), 0);
#endif

    /*File browser container style*/
    lv_obj_set_style_pad_all(explorer->browser_area, 0, 0);
    lv_obj_set_style_pad_row(explorer->browser_area, 0, 0);
    lv_obj_set_style_radius(explorer->browser_area, 0, 0);
    lv_obj_set_style_border_width(explorer->browser_area, 0, 0);
    lv_obj_set_style_outline_width(explorer->browser_area, 0, 0);
    lv_obj_set_style_bg_color(explorer->browser_area, lv_color_hex(0xffffff), 0);

    /*Style of the table in the browser container*/
    lv_obj_set_style_bg_color(explorer->file_table, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(explorer->file_table, 0, 0);
    lv_obj_set_style_radius(explorer->file_table, 0, 0);
    lv_obj_set_style_border_width(explorer->file_table, 0, 0);
    lv_obj_set_style_outline_width(explorer->file_table, 0, 0);

#if LV_FILE_EXPLORER_QUICK_ACCESS
    /*Style of the list in the quick access bar*/
    lv_obj_set_style_border_width(explorer->list_device, 0, 0);
    lv_obj_set_style_outline_width(explorer->list_device, 0, 0);
    lv_obj_set_style_radius(explorer->list_device, 0, 0);
    lv_obj_set_style_pad_all(explorer->list_device, 0, 0);

    lv_obj_set_style_border_width(explorer->list_places, 0, 0);
    lv_obj_set_style_outline_width(explorer->list_places, 0, 0);
    lv_obj_set_style_radius(explorer->list_places, 0, 0);
    lv_obj_set_style_pad_all(explorer->list_places, 0, 0);

    /*Style of the quick access list btn in the quick access bar*/
    lv_style_init(&quick_access_list_button_style);
    lv_style_set_border_width(&quick_access_list_button_style, 0);
    lv_style_set_bg_color(&quick_access_list_button_style, lv_color_hex(0xf2f1f6));

    uint32_t i, j;
    for(i = 0; i < lv_obj_get_child_count(explorer->quick_access_area); i++) {
        lv_obj_t * child = lv_obj_get_child(explorer->quick_access_area, i);
        if(lv_obj_check_type(child, &lv_list_class)) {
            for(j = 0; j < lv_obj_get_child_count(child); j++) {
                lv_obj_t * list_child = lv_obj_get_child(child, j);
                if(lv_obj_check_type(list_child, &lv_list_button_class)) {
                    lv_obj_add_style(list_child, &quick_access_list_button_style, 0);
                }
            }
        }
    }
#endif

}

#if LV_FILE_EXPLORER_QUICK_ACCESS
static void quick_access_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_current_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    if(code == LV_EVENT_CLICKED) {
        char ** path = NULL;
        lv_obj_t * label = lv_obj_get_child(btn, -1);
        char * label_text = lv_label_get_text(label);

        if((lv_strcmp(label_text, LV_SYMBOL_HOME " HOME") == 0)) {
            path = &(explorer->home_dir);
        }
        else if((lv_strcmp(label_text, LV_SYMBOL_VIDEO " Video") == 0)) {
            path = &(explorer->video_dir);
        }
        else if((lv_strcmp(label_text, LV_SYMBOL_IMAGE " Pictures") == 0)) {
            path = &(explorer->pictures_dir);
        }
        else if((lv_strcmp(label_text, LV_SYMBOL_AUDIO " Music") == 0)) {
            path = &(explorer->music_dir);
        }
        else if((lv_strcmp(label_text, LV_SYMBOL_FILE "  Documents") == 0)) {
            path = &(explorer->docs_dir);
        }
        else if((lv_strcmp(label_text, LV_SYMBOL_DRIVE " File System") == 0)) {
            path = &(explorer->fs_dir);
        }

        if(path != NULL)
            show_dir(obj, *path);
    }
}

static void quick_access_area_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * area = lv_event_get_current_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    if(code == LV_EVENT_LAYOUT_CHANGED) {
        if(lv_obj_has_flag(area, LV_OBJ_FLAG_HIDDEN))
            lv_obj_set_size(explorer->browser_area, LV_PCT(100), LV_PCT(100));
        else
            lv_obj_set_size(explorer->browser_area, LV_PCT(FILE_EXPLORER_BROWSER_AREA_WIDTH), LV_PCT(100));
    }
}
#endif

static void browser_file_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    //               (_lv_obj_t*) 
    lv_obj_t * obj = (_lv_obj_t*) lv_event_get_user_data(e);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    if(code == LV_EVENT_VALUE_CHANGED) {
        char file_name[LV_FILE_EXPLORER_PATH_MAX_LEN];
        const char * str_fn = NULL;
        uint32_t row;
        uint32_t col;

        lv_memzero(file_name, sizeof(file_name));
        lv_table_get_selected_cell(explorer->file_table, &row, &col);
        str_fn = lv_table_get_cell_value(explorer->file_table, row, col);

        str_fn = str_fn + 5;
        if((lv_strcmp(str_fn, ".") == 0))  return;

        if((lv_strcmp(str_fn, "..") == 0) && (lv_strlen(explorer->current_path) > 3)) {
            strip_ext(explorer->current_path);
            /*Remove the last '/' character*/
            strip_ext(explorer->current_path);
            lv_snprintf((char *)file_name, sizeof(file_name), "%s", explorer->current_path);
        }
        else {
            if(lv_strcmp(str_fn, "..") != 0) {
                lv_snprintf((char *)file_name, sizeof(file_name), "%s%s", explorer->current_path, str_fn);
            }
        }

        lv_fs_dir_t dir;
        if(lv_fs_dir_open(&dir, file_name) == LV_FS_RES_OK) {
            lv_fs_dir_close(&dir);
            show_dir(obj, (char *)file_name);
        }
        else {
            if(lv_strcmp(str_fn, "..") != 0) {
                explorer->sel_fn = str_fn;
                lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
            }
        }
    }
    else if(code == LV_EVENT_SIZE_CHANGED) {
        lv_table_set_column_width(explorer->file_table, 0, lv_obj_get_width(explorer->file_table));
    }
    else if((code == LV_EVENT_CLICKED) || (code == LV_EVENT_RELEASED)) {
        lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
    }
}

static void show_dir(lv_obj_t * obj, const char * path)
{
    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    char fn[LV_FILE_EXPLORER_PATH_MAX_LEN];
    uint16_t index = 0;
    lv_fs_res_t res;

    // No need to "open directory" for now TODO: Check if makes sense to validate that exists
    ESP_LOGI(TAG, "dir_open %s", path);
    dir = opendir(path);

    if (dir == NULL) {
        LV_LOG_USER("Open dir error");
        return;
    }

    lv_table_set_cell_value_fmt(explorer->file_table, index++, 0, LV_SYMBOL_DIRECTORY "  %s", ".");
    lv_table_set_cell_value_fmt(explorer->file_table, index++, 0, LV_SYMBOL_DIRECTORY "  %s", "..");
    lv_table_set_cell_value(explorer->file_table, 0, 1, "0");
    lv_table_set_cell_value(explorer->file_table, 1, 1, "0");

    while ((dp = readdir (dir)) != NULL) {
        /*fn is empty, if not more files to read*/
        strcpy(fn, dp->d_name);
        if(lv_strlen(fn) == 0) {
            LV_LOG_USER("Not more files to read!");
            break;
        }

        if((is_end_with(fn, ".png") == true)  || (is_end_with(fn, ".PNG") == true)  || \
           (is_end_with(fn, ".jpg") == true) || (is_end_with(fn, ".JPG") == true) || \
           (is_end_with(fn, ".bmp") == true) || (is_end_with(fn, ".BMP") == true) || \
           (is_end_with(fn, ".gif") == true) || (is_end_with(fn, ".GIF") == true)) {
            lv_table_set_cell_value_fmt(explorer->file_table, index, 0, LV_SYMBOL_IMAGE "  %s", fn);
            lv_table_set_cell_value(explorer->file_table, index, 1, "1");
        }
        else if((is_end_with(fn, ".mp3") == true) || (is_end_with(fn, ".MP3") == true) || \
                (is_end_with(fn, ".wav") == true) || (is_end_with(fn, ".WAV") == true)) {
            lv_table_set_cell_value_fmt(explorer->file_table, index, 0, LV_SYMBOL_AUDIO "  %s", fn);
            lv_table_set_cell_value(explorer->file_table, index, 1, "2");
        }
        else if((is_end_with(fn, ".mp4") == true) || (is_end_with(fn, ".MP4") == true)) {
            lv_table_set_cell_value_fmt(explorer->file_table, index, 0, LV_SYMBOL_VIDEO "  %s", fn);
            lv_table_set_cell_value(explorer->file_table, index, 1, "3");
        }
        else if((is_end_with(fn, ".") == true) || (is_end_with(fn, "..") == true)) {
            /*is dir*/
            continue;
        }
        else if(fn[0] == '/') {/*is dir*/
            lv_table_set_cell_value_fmt(explorer->file_table, index, 0, LV_SYMBOL_DIRECTORY "  %s", fn + 1);
            lv_table_set_cell_value(explorer->file_table, index, 1, "0");
        }
        else {
            lv_table_set_cell_value_fmt(explorer->file_table, index, 0, LV_SYMBOL_FILE "  %s", fn);
            lv_table_set_cell_value(explorer->file_table, index, 1, "4");
        }

        index++;
    }

     closedir(dir);

    lv_table_set_row_count(explorer->file_table, index);
    file_explorer_sort(obj);
    lv_obj_send_event(obj, LV_EVENT_READY, NULL);

    /*Move the table to the top*/
    lv_obj_scroll_to_y(explorer->file_table, 0, LV_ANIM_OFF);

    lv_memzero(explorer->current_path, sizeof(explorer->current_path));
    lv_strncpy(explorer->current_path, path, sizeof(explorer->current_path) - 1);
    lv_label_set_text_fmt(explorer->path_label, LV_SYMBOL_EYE_OPEN" %s", path);

    size_t current_path_len = lv_strlen(explorer->current_path);
    if((*((explorer->current_path) + current_path_len) != '/') && (current_path_len < LV_FILE_EXPLORER_PATH_MAX_LEN)) {
        *((explorer->current_path) + current_path_len) = '/';
    }
}

/*Remove the specified suffix*/
static void strip_ext(char * dir)
{
    char * end = dir + lv_strlen(dir);

    while(end >= dir && *end != '/') {
        --end;
    }

    if(end > dir) {
        *end = '\0';
    }
    else if(end == dir) {
        *(end + 1) = '\0';
    }
}

static void exch_table_item(lv_obj_t * tb, int16_t i, int16_t j)
{
    const char * tmp;
    tmp = lv_table_get_cell_value(tb, i, 0);
    lv_table_set_cell_value(tb, 0, 2, tmp);
    lv_table_set_cell_value(tb, i, 0, lv_table_get_cell_value(tb, j, 0));
    lv_table_set_cell_value(tb, j, 0, lv_table_get_cell_value(tb, 0, 2));

    tmp = lv_table_get_cell_value(tb, i, 1);
    lv_table_set_cell_value(tb, 0, 2, tmp);
    lv_table_set_cell_value(tb, i, 1, lv_table_get_cell_value(tb, j, 1));
    lv_table_set_cell_value(tb, j, 1, lv_table_get_cell_value(tb, 0, 2));
}

static void file_explorer_sort(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_file_explorer_t * explorer = (lv_file_explorer_t *)obj;

    uint16_t sum = lv_table_get_row_count(explorer->file_table);

    if(sum > 1) {
        switch(explorer->sort) {
            case LV_EXPLORER_SORT_NONE:
                break;
            case LV_EXPLORER_SORT_KIND:
                sort_by_file_kind(explorer->file_table, 0, (sum - 1));
                break;
            default:
                break;
        }
    }
}

/*Quick sort 3 way*/
static void sort_by_file_kind(lv_obj_t * tb, int16_t lo, int16_t hi)
{
    if(lo >= hi) return;

    int16_t lt = lo;
    int16_t i = lo + 1;
    int16_t gt = hi;
    const char * v = lv_table_get_cell_value(tb, lo, 1);
    while(i <= gt) {
        if(lv_strcmp(lv_table_get_cell_value(tb, i, 1), v) < 0)
            exch_table_item(tb, lt++, i++);
        else if(lv_strcmp(lv_table_get_cell_value(tb, i, 1), v) > 0)
            exch_table_item(tb, i, gt--);
        else
            i++;
    }

    sort_by_file_kind(tb, lo, lt - 1);
    sort_by_file_kind(tb, gt + 1, hi);
}

static bool is_end_with(const char * str1, const char * str2)
{
    if(str1 == NULL || str2 == NULL)
        return false;

    uint16_t len1 = lv_strlen(str1);
    uint16_t len2 = lv_strlen(str2);
    if((len1 < len2) || (len1 == 0 || len2 == 0))
        return false;

    while(len2 >= 1) {
        if(str2[len2 - 1] != str1[len1 - 1])
            return false;

        len2--;
        len1--;
    }

    return true;
}
