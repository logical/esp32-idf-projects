/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_hf.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "time.h"
#include "sys/time.h"
#include "sdkconfig.h"

#include "driver/i2s_std.h"
#include "sd_card_player.h"

#include "nvs.h"
#include "nvs_flash.h"


extern    i2s_chan_handle_t                tx_chan;        // I2S tx channel handler
extern    i2s_chan_handle_t                rx_chan;        // I2S rx channel handler



const char *c_hf_evt_str[] = {
    "CONNECTION_STATE_EVT",              /*!< connection state changed event */
    "AUDIO_STATE_EVT",                   /*!< audio connection state change event */
    "VR_STATE_CHANGE_EVT",                /*!< voice recognition state changed */
    "CALL_IND_EVT",                      /*!< call indication event */
    "CALL_SETUP_IND_EVT",                /*!< call setup indication event */
    "CALL_HELD_IND_EVT",                 /*!< call held indicator event */
    "NETWORK_STATE_EVT",                 /*!< network state change event */
    "SIGNAL_STRENGTH_IND_EVT",           /*!< signal strength indication event */
    "ROAMING_STATUS_IND_EVT",            /*!< roaming status indication event */
    "BATTERY_LEVEL_IND_EVT",             /*!< battery level indication event */
    "CURRENT_OPERATOR_EVT",              /*!< current operator name event */
    "RESP_AND_HOLD_EVT",                 /*!< response and hold event */
    "CLIP_EVT",                          /*!< Calling Line Identification notification event */
    "CALL_WAITING_EVT",                  /*!< call waiting notification */
    "CLCC_EVT",                          /*!< listing current calls event */
    "VOLUME_CONTROL_EVT",                /*!< audio volume control event */
    "AT_RESPONSE",                       /*!< audio volume control event */
    "SUBSCRIBER_INFO_EVT",               /*!< subscriber information event */
    "INBAND_RING_TONE_EVT",              /*!< in-band ring tone settings */
    "LAST_VOICE_TAG_NUMBER_EVT",         /*!< requested number from AG event */
    "RING_IND_EVT",                      /*!< ring indication event */
};

// esp_hf_client_connection_state_t
const char *c_connection_state_str[] = {
    "disconnected",
    "connecting",
    "connected",
    "slc_connected",
    "disconnecting",
};

// esp_hf_client_audio_state_t
const char *c_audio_state_str[] = {
    "disconnected",
    "connecting",
    "connected",
    "connected_msbc",
};

/// esp_hf_vr_state_t
const char *c_vr_state_str[] = {
    "disabled",
    "enabled",
};

// esp_hf_service_availability_status_t
const char *c_service_availability_status_str[] = {
    "unavailable",
    "available",
};

// esp_hf_roaming_status_t
const char *c_roaming_status_str[] = {
    "inactive",
    "active",
};

// esp_hf_client_call_state_t
const char *c_call_str[] = {
    "NO call in progress",
    "call in progress",
};

// esp_hf_client_callsetup_t
const char *c_call_setup_str[] = {
    "NONE",
    "INCOMING",
    "OUTGOING_DIALING",
    "OUTGOING_ALERTING"
};

// esp_hf_client_callheld_t
const char *c_call_held_str[] = {
    "NONE held",
    "Held and Active",
    "Held",
};

// esp_hf_response_and_hold_status_t
const char *c_resp_and_hold_str[] = {
    "HELD",
    "HELD ACCEPTED",
    "HELD REJECTED",
};

// esp_hf_client_call_direction_t
const char *c_call_dir_str[] = {
    "outgoing",
    "incoming",
};

// esp_hf_client_call_state_t
const char *c_call_state_str[] = {
    "active",
    "held",
    "dialing",
    "alerting",
    "incoming",
    "waiting",
    "held_by_resp_hold",
};

// esp_hf_current_call_mpty_type_t
const char *c_call_mpty_type_str[] = {
    "single",
    "multi",
};

// esp_hf_volume_control_target_t
const char *c_volume_control_target_str[] = {
    "SPEAKER",
    "MICROPHONE"
};

// esp_hf_at_response_code_t
const char *c_at_response_code_str[] = {
    "OK",
    "ERROR"
    "ERR_NO_CARRIER",
    "ERR_BUSY",
    "ERR_NO_ANSWER",
    "ERR_DELAYED",
    "ERR_BLACKLILSTED",
    "ERR_CME",
};

// esp_hf_subscriber_service_type_t
const char *c_subscriber_service_type_str[] = {
    "unknown",
    "voice",
    "fax",
};

// esp_hf_client_in_band_ring_state_t
const char *c_inband_ring_state_str[] = {
    "NOT provided",
    "Provided",
};

extern esp_bd_addr_t peer_addr;
// If you want to connect a specific device, add it's address here
//esp_bd_addr_t peer_addr = {0xac, 0x67, 0xb2, 0x53, 0x77, 0xbe};

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI

#define VOLUME_ADJUST 2

#define AUDIO_RATE 8000
bool dataReady=true;
uint8_t control_num=0;
//TaskHandle_t timerHandle = NULL;
/*
void timerTask( void * pvParameters )
{
 while(1)
 {
    vTaskDelay(pdMS_TO_TICKS(4.5));
    dataReady=true;
 }
}
*/

static void bt_app_hf_client_audio_open(void)
{
    mp3Stop();
    vTaskDelay(pdMS_TO_TICKS(10));
    i2sChangeSampleRate(8000);
//    xTaskCreate( timerTask, "NAME", 32768, NULL, tskIDLE_PRIORITY, &timerHandle );
    control_num=0;


}

static void bt_app_hf_client_audio_close(void)
{
//    vTaskDelete( timerHandle );
    vTaskDelay(pdMS_TO_TICKS(10));
    mp3Start();

}

static uint32_t bt_app_hf_client_outgoing_cb(uint8_t *p_buf, uint32_t sz)
{
//convert signed data from microphone to unsigned data
    size_t bytes = 0;

    //    ESP_LOGI(BT_HF_TAG,"bytes %lu",sz);

    int32_t convBuff[sz*2];//microphone picks up 24 bits in 32 bit blocks in stereo
    i2s_channel_read(rx_chan,convBuff , sz*2, &bytes, 0);

    if(bytes==sz*2){
        for(int i=0; i < bytes/sizeof(int16_t); i++){

            int32_t data=((~convBuff[i]+1)>>8)/4;// 2's complement

            *(int16_t*)(&p_buf[i*sizeof(int16_t)])=data;

        }

        return sz;
    }
    return 0;
}

static void bt_app_hf_client_incoming_cb(const uint8_t *buf, uint32_t sz)
{

        size_t bytes = 0;

////////////convert the data
        int32_t outbuf[sz*2];
        for(int i=0;i<sz/sizeof(int16_t);i++){
            int32_t data = *((int16_t*)buf+i*sizeof(int16_t))*4096 ;
 //           if(i%16)ESP_LOGI(BT_HF_TAG,"DATA = %li",data);
            outbuf[i*2] = data;///stereo
            outbuf[i*2+1] = data;
        }

    i2s_channel_write(tx_chan, outbuf, sz*sizeof(int32_t)/sizeof(int16_t), &bytes, portMAX_DELAY);
    control_num++;if (control_num >=8){ control_num = 0; esp_hf_client_outgoing_data_ready();}
//    if(dataReady){esp_hf_client_outgoing_data_ready();dataReady=false;}

}

#endif /* #if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

/* callback for HF_CLIENT */
void bt_app_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    if (event <= ESP_HF_CLIENT_RING_IND_EVT) {
        ESP_LOGI(BT_HF_TAG, "APP HFP event: %s", c_hf_evt_str[event]);
    } else {
        ESP_LOGE(BT_HF_TAG, "APP HFP invalid event %d", event);
    }

    switch (event) {
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--connection state %s, peer feats 0x%"PRIx32", chld_feats 0x%"PRIx32,
                    c_connection_state_str[param->conn_stat.state],
                    param->conn_stat.peer_feat,
                    param->conn_stat.chld_feat);
            memcpy(peer_addr,param->conn_stat.remote_bda,ESP_BD_ADDR_LEN);
            ESP_LOGI(BT_HF_TAG, "address is %d:%d:%d:%d:%d:%d",peer_addr[0],peer_addr[1],peer_addr[2],peer_addr[3],peer_addr[4],peer_addr[5]);

//            save the address
//            size_t required_size = 0;  // value will default to 0, if not set yet in NVS
            nvs_handle_t my_handle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");

            err=nvs_set_u8(my_handle, "peer_addr[0]", peer_addr[0]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            err=nvs_set_u8(my_handle, "peer_addr[1]", peer_addr[1]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            err=nvs_set_u8(my_handle, "peer_addr[2]", peer_addr[2]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            err=nvs_set_u8(my_handle, "peer_addr[3]", peer_addr[3]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            err=nvs_set_u8(my_handle, "peer_addr[4]", peer_addr[4]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            err=nvs_set_u8(my_handle, "peer_addr[5]", peer_addr[5]);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");

            err=nvs_commit(my_handle);
            if (err != ESP_OK)ESP_LOGE(BT_HF_TAG, "error saving nvs");
            nvs_close(my_handle);

            break;
        }

        case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--audio state %s",
                    c_audio_state_str[param->audio_stat.state]);
    #if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
            if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED || param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
                bt_app_hf_client_audio_open();
                esp_hf_client_register_data_callback(bt_app_hf_client_incoming_cb,bt_app_hf_client_outgoing_cb);
            }
            else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
                bt_app_hf_client_audio_close();
           }
    #endif /* #if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */
            break;
        }

        case ESP_HF_CLIENT_BVRA_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--VR state %s",
                    c_vr_state_str[param->bvra.value]);
            break;
        }

        case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--NETWORK STATE %s",
                    c_service_availability_status_str[param->service_availability.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--ROAMING: %s",
                    c_roaming_status_str[param->roaming.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "-- signal strength: %d",
                    param->signal_strength.value);
            break;
        }

        case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--battery level %d",
                    param->battery_level.value);
            break;
        }

        case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--operator name: %s",
                    param->cops.name);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--Call indicator %s",
                    c_call_str[param->call.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--Call setup indicator %s",
                    c_call_setup_str[param->call_setup.status]);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--Call held indicator %s",
                    c_call_held_str[param->call_held.status]);
            break;
        }

        case ESP_HF_CLIENT_BTRH_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--response and hold %s",
                    c_resp_and_hold_str[param->btrh.status]);
            break;
        }

        case ESP_HF_CLIENT_CLIP_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--clip number %s",
                    (param->clip.number == NULL) ? "NULL" : (param->clip.number));
            break;
        }

        case ESP_HF_CLIENT_CCWA_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--call_waiting %s",
                    (param->ccwa.number == NULL) ? "NULL" : (param->ccwa.number));
            break;
        }

        case ESP_HF_CLIENT_CLCC_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--Current call: idx %d, dir %s, state %s, mpty %s, number %s",
                    param->clcc.idx,
                    c_call_dir_str[param->clcc.dir],
                    c_call_state_str[param->clcc.status],
                    c_call_mpty_type_str[param->clcc.mpty],
                    (param->clcc.number == NULL) ? "NULL" : (param->clcc.number));
            break;
        }

        case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--volume_target: %s, volume %d",
                    c_volume_control_target_str[param->volume_control.type],
                    param->volume_control.volume);
            break;
        }

        case ESP_HF_CLIENT_AT_RESPONSE_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--AT response event, code %d, cme %d",
                    param->at_response.code, param->at_response.cme);
            break;
        }

        case ESP_HF_CLIENT_CNUM_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--subscriber type %s, number %s",
                    c_subscriber_service_type_str[param->cnum.type],
                    (param->cnum.number == NULL) ? "NULL" : param->cnum.number);
            break;
        }

        case ESP_HF_CLIENT_BSIR_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--inband ring state %s",
                    c_inband_ring_state_str[param->bsir.state]);
            break;
        }

        case ESP_HF_CLIENT_BINP_EVT:
        {
            ESP_LOGI(BT_HF_TAG, "--last voice tag number: %s",
                    (param->binp.number == NULL) ? "NULL" : param->binp.number);
            break;
        }

        default:
            ESP_LOGE(BT_HF_TAG, "HF_CLIENT EVT: %d", event);
            break;
    }
}
