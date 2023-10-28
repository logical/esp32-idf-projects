/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "bt_app_core.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "bt_app_hf.h"
#include "esp_console.h"
#include "app_hf_msg_set.h"

#include "components/ssd1306/ssd1306.h"
#include "components/ssd1306/font8x8_basic.h"

#include "sd_card_player.h"

#include "FMTX.h"

//default is 86.0
float frequency=92;

/////////////////////////////////////
// DISPLAY and fm tx
////////////////////////////////////
#define  CONFIG_SDA_GPIO 21
#define  CONFIG_SCL_GPIO 22
#define  CONFIG_RESET_GPIO -1



/////////////////////////////////////
// rotary encoder
/////////////////////////////////////
#define CLK 19
#define DT 18
#define SW 4  //PIN 4 will wake device from deep sleep



//SSD1306Wire display(0x3c,SDA,SCL);   // ADDRESS, SDA, SCL PINS 21 AND 22




esp_bd_addr_t peer_addr = {0};
static char peer_bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
static uint8_t peer_bdname_len;

static const char remote_device_name[] = "ESP_HFP_AG";

static bool get_name_from_eir(uint8_t *eir, char *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        for (int i = 0; i < param->disc_res.num_prop; i++){
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
                && get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)){
                if (strcmp(peer_bdname, remote_device_name) == 0) {
                    memcpy(peer_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
                    ESP_LOGI(BT_HF_TAG, "Found a target device address: \n");
                    esp_log_buffer_hex(BT_HF_TAG, peer_addr, ESP_BD_ADDR_LEN);
                    ESP_LOGI(BT_HF_TAG, "Found a target device name: %s", peer_bdname);
                    printf("Connect.\n");
                    esp_hf_client_connect(peer_addr);
                    esp_bt_gap_cancel_discovery();
                }
            }
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT");
    case ESP_BT_GAP_RMT_SRVCS_EVT:
    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_HF_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(BT_HF_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_HF_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(BT_HF_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(BT_HF_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%"PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(BT_HF_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
        break;

    default: {
        ESP_LOGI(BT_HF_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

/* event for handler "bt_av_hdl_stack_up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/* handler for bluetooth stack enabled events */
static void bt_hf_client_hdl_stack_evt(uint16_t event, void *p_param);

void app_main(void)
{
	SSD1306_t dev;

    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO,-1);//fmtx and the display use the same pins
	ssd1306_init(&dev, 128, 64);
	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);
	ssd1306_display_text(&dev, 0, "Hello", 5, false);
//	vTaskDelay(3000 / portTICK_PERIOD_MS);


    fmtx_init(frequency, USA);



    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(BT_HF_TAG, "ERASING NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
esp_err_t err ;
    /*

    ///GET SETTINGS SAVED IN MEMORY
    nvs_handle_t my_handle;
    err= nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs storage");
    err = nvs_get_u8(my_handle, "peer_addr[0]", &peer_addr[0]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs1");
    err = nvs_get_u8(my_handle, "peer_addr[1]", &peer_addr[1]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs2");
    err = nvs_get_u8(my_handle, "peer_addr[2]", &peer_addr[2]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs3");
    err = nvs_get_u8(my_handle, "peer_addr[3]", &peer_addr[3]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs4");
    err = nvs_get_u8(my_handle, "peer_addr[4]", &peer_addr[4]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs5");
    err = nvs_get_u8(my_handle, "peer_addr[5]", &peer_addr[5]);
    if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error opening nvs6");
    nvs_close(my_handle);
    ESP_LOGI(BT_HF_TAG, "address is %d:%d:%d:%d:%d:%d",peer_addr[0],peer_addr[1],peer_addr[2],peer_addr[3],peer_addr[4],peer_addr[5]);

if(peer_addr[0]!=0){
    esp_hf_client_connect(peer_addr);
    esp_hf_client_connect_audio(peer_addr);
    esp_bt_gap_cancel_discovery();
}
*/

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

//    esp_err_t err;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
//esp_bluedroid_init_with_cfg
    esp_bluedroid_config_t esp_bluedroid_init_cfg=BT_BLUEDROID_INIT_CONFIG_DEFAULT();

    if ((err = esp_bluedroid_init_with_cfg(&esp_bluedroid_init_cfg)) != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    /*deprecated
    if ((err = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
    */

    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    /* create application task */
    bt_app_task_start_up();

    /* Bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_hf_client_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL);

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_PCM
    /* configure the PCM interface and PINs used */
//    app_gpio_pcm_io_cfg();
#endif

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
//    app_gpio_hci_io_cfg();
#endif
    /* configure externel chip for acoustic echo cancellation */
#if ACOUSTIC_ECHO_CANCELLATION_ENABLE
//    app_gpio_aec_io_cfg();
#endif /* ACOUSTIC_ECHO_CANCELLATION_ENABLE */

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = "hfp_hf>";

/*START THE LAST ADDRESS
if(peer_addr[0]!=0){
    esp_hf_client_connect(peer_addr);
    esp_hf_client_connect_audio(peer_addr);
    esp_bt_gap_cancel_discovery();
}
*/

    startSDCard();
    mp3Start();
//    i2sInputStart();


    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    /* Register commands */
    register_hfp_hf();
    printf("\n ==================================================\n");
    printf(" |       Steps to test hfp_hf                     |\n");
    printf(" |                                                |\n");
    printf(" |  1. Print 'help' to gain overview of commands  |\n");
    printf(" |  2. Setup a service level connection           |\n");
    printf(" |  3. Run hfp_hf to test                         |\n");
    printf(" |                                                |\n");
    printf(" =================================================\n\n");






    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));



}


static void bt_hf_client_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_HF_TAG, "%s evt %d", __func__, event);
    switch (event) {
    case BT_APP_EVT_STACK_UP: {
        /* set up device name */
        char *dev_name = "ESP_HFP_HF";
        esp_bt_dev_set_device_name(dev_name);

        /* register GAP callback function */
        esp_bt_gap_register_callback(esp_bt_gap_cb);
        esp_hf_client_register_callback(bt_app_hf_client_cb);
        esp_hf_client_init();

        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
        esp_bt_pin_code_t pin_code;
        pin_code[0] = '0';
        pin_code[1] = '0';
        pin_code[2] = '0';
        pin_code[3] = '0';
        esp_bt_gap_set_pin(pin_type, 4, pin_code);

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

        /* start device discovery */
        ESP_LOGI(BT_HF_TAG, "Starting device discovery...");
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
        break;
    }
    default:
        ESP_LOGE(BT_HF_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}
