/* SD card and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <strings.h>
#include <dirent.h>
#include <ctype.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"

#include <lwip/netdb.h>
#include <lwip/dns.h>


static const char *TAG = "server";

#define MOUNT_POINT "/sdcard"

// change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  23
#define PIN_NUM_CLK   18
#define PIN_NUM_CS    5

#define EXAMPLE_ESP_WIFI_SSID      "esp32"
#define EXAMPLE_ESP_WIFI_PASS      ""
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       10

void receive_thread(void *pvParameters) {
    int socket_fd;
//    struct sockaddr_in sa, ra;
    struct sockaddr_in ra;



    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0){
        ESP_LOGE(TAG, "Failed to create socket");
        exit(0);
    }

//    memset(&sa, 0, sizeof(struct sockaddr_in));

//    tcpip_adapter_ip_info_t ip;
//    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip);
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(IP_EVENT_STA_GOT_IP,&ip);
    ra.sin_family = AF_INET;
    ip.ip.addr=(uint32_t)0x0104a8c0; //192.168.4.1
    ra.sin_addr.s_addr = ip.ip.addr;
//    ra.sin_addr.s_addr = (uint32_t)0xc0a80401;
      ESP_LOGI(TAG,"My IP: " IPSTR "\n", IP2STR(&ip.ip));
    ra.sin_port = htons(53);
    if (bind(socket_fd, (struct sockaddr *)&ra, sizeof(struct sockaddr_in)) == -1) {
        ESP_LOGE(TAG, "Failed to bind to 53/udp");
        close(socket_fd);
        exit(1);
    }

    struct sockaddr_in client;
    socklen_t client_len;
    client_len = sizeof(client);
    int length;
    char data[80];
    char response[100];
    char ipAddress[INET_ADDRSTRLEN];
    int idx;
    int err;

    ESP_LOGI(TAG, "DNS Server listening on 53/udp");
    while (1) {
        length = recvfrom(socket_fd, data, sizeof(data), 0, (struct sockaddr *)&client, &client_len);
        if (length > 0) {
            data[length] = '\0';

            inet_ntop(AF_INET, &(client.sin_addr), ipAddress, INET_ADDRSTRLEN);
            ESP_LOGI(TAG, "Replying to DNS request (len=%d) from %s", length, ipAddress);

            // Prepare our response
            response[0] = data[0];
            response[1] = data[1];
            response[2] = 0b10000100 | (0b00000001 & data[2]); //response, authorative answer, not truncated, copy the recursion bit
            response[3] = 0b00000000; //no recursion available, no errors
            response[4] = data[4];
            response[5] = data[5]; //Question count
            response[6] = data[4];
            response[7] = data[5]; //answer count
            response[8] = 0x00;
            response[9] = 0x00;       //NS record count
            response[10]= 0x00;
            response[11]= 0x00;       //Resource record count

            memcpy(response+12, data+12, length-12); //Copy the rest of the query section
            idx = length;

            // Prune off the OPT
            // FIXME: We should parse the packet better than this!
            if ((response[idx-11] == 0x00) && (response[idx-10] == 0x00) && (response[idx-9] == 0x29))
                idx -= 11;

            //Set a pointer to the domain name in the question section
            response[idx] = 0xC0;
            response[idx+1] = 0x0C;

            //Set the type to "Host Address"
            response[idx+2] = 0x00;
            response[idx+3] = 0x01;

            //Set the response class to IN
            response[idx+4] = 0x00;
            response[idx+5] = 0x01;

            //A 32 bit integer specifying TTL in seconds, 0 means no caching
            response[idx+6] = 0x00;
            response[idx+7] = 0x00;
            response[idx+8] = 0x00;
            response[idx+9] = 0x00;

            //RDATA length
            response[idx+10] = 0x00;
            response[idx+11] = 0x04; //4 byte IP address

            //The IP address
            response[idx + 12] = 192;
            response[idx + 13] = 168;
            response[idx + 14] = 4;
            response[idx + 15] = 1;

            err = sendto(socket_fd, response, idx+16, 0, (struct sockaddr *)&client, client_len);
            if (err < 0) {
                ESP_LOGE(TAG, "sendto failed: %s", strerror(errno));
            }
        }
    }
    close(socket_fd);
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

const char* getExtension(const char * filename){
  const char* index =  strchr(filename,'.');
  ESP_LOGI(TAG,"extension:%s",index);
  return index;
}

httpd_handle_t stream_httpd = NULL;
httpd_config_t config = HTTPD_DEFAULT_CONFIG();

#define FILE_SIZE 1024


static esp_err_t sendIndex(httpd_req_t* req, httpd_err_code_t err)
{

    char html[FILE_SIZE];
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "html");
    const char* indexName=MOUNT_POINT"/index.html";
    ESP_LOGI(TAG,"opening: %s", indexName);

     FILE *f=fopen(indexName,"r");
    int c;
    int n=0;
    while ((c = fgetc(f)) != EOF)
    {
        html[n++] = (char)c;
    }

    // don't forget to terminate with the null character
    html[n] = '\0';
    fclose(f);
//    ESP_LOGI(TAG,"sending: %s", html);

    return httpd_resp_send(req,html,strlen(html));

}

static esp_err_t index_handler(httpd_req_t *req)
{

//  ESP_LOGI(TAG,req->uri);
    char html[FILE_SIZE];
    const char* ext = getExtension(req->uri);

//    httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND,sendIndex);


  if (!strcmp(ext,".html") || !strcmp(ext,".htm")|| !strcmp(ext,".txt")){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "html");
  }

  if (!strcmp(ext,".js")){
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_set_hdr(req, "Content-Encoding", "text");
  }
  if (!strcmp(ext,".css")){
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Content-Encoding", "text");
  }
  if (!strcmp(ext,".png")){
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Content-Encoding", "png");
  }
  if (!strcmp(ext,".jpg")){
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Encoding", "jpg");
  }
    char fileName[768];
    sprintf(fileName,"%s%s",MOUNT_POINT,req->uri);
    ESP_LOGI(TAG,"opening: %s", fileName);

     FILE *f=fopen(fileName,"r");
    int c;
    int n=0;
    while ((c = fgetc(f)) != EOF)
    {
        html[n++] = (char)c;
    }

    // don't forget to terminate with the null character
    html[n] = '\0';
    fclose(f);
//    ESP_LOGI(TAG,"sending: %s", html);

    return httpd_resp_send(req,html,strlen(html));

}

//i could only get about 500 files to work without error

#define MAXIMUM_FILES 500
uint32_t filecount=0;
FILE* indexPageFile;

void listDir(const char * path){
    DIR *d;
    struct dirent *dir;
    char dirPath[512];
    sprintf(dirPath,"%s%s",MOUNT_POINT,path);


  d = opendir(dirPath);
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      //if file is directory
        if(dir->d_type==DT_DIR){
//            char dirPath[512];
            sprintf(dirPath,"%s%s/",path,dir->d_name);
            listDir(dirPath);

            continue;
        }

        const char* ext = getExtension(dir->d_name);
        if( (strcmp(ext,".html")==0)||
            (strcmp(ext,".htm")==0)||
            (strcmp(ext,".js")==0) ||
            (strcmp(ext,".css")==0) ||
            (strcmp(ext,".txt")==0) ||
            (strcmp(ext,".png")==0) ||
            (strcmp(ext,".jpg")==0)){
            char fileName[256];
            sprintf(fileName,"%s%s",path,dir->d_name);
        //put links in index.html
 //           fprintf(indexPageFile,"<a href=\"%s\">%s</a><br>\n",fileName,fileName);

            ESP_LOGI(TAG,"filename listed: %s", fileName);

            httpd_uri_t indexAllUri={
            .uri = strcpy(malloc(strlen(fileName)+1),fileName),
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL,
            };
            httpd_register_uri_handler(stream_httpd, &indexAllUri);
            filecount++;

        }
       if(filecount>MAXIMUM_FILES)break;

    }
    closedir(d);
  }

}


void app_main(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
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
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    //Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

//initialize webserver
    config.max_uri_handlers = MAXIMUM_FILES;
    httpd_start(&stream_httpd, &config);
    httpd_register_err_handler(stream_httpd, HTTPD_404_NOT_FOUND,sendIndex);

    const char* indexName=MOUNT_POINT"/index.html";

    indexPageFile=fopen(indexName,"w+");
    fprintf(indexPageFile,"<html>\n<body>");
    listDir("/");
    fprintf(indexPageFile,"</body>\n</html>");
    fclose(indexPageFile);
    ESP_LOGI(TAG,"LISTING SUCCESSFUL");


//    xTaskCreate(&receive_thread, "receive_thread", 3048, NULL, 5, NULL);


}
