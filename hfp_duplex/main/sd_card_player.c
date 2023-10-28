/* SD card and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "driver/i2s_std.h"

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <strings.h>
#include <dirent.h>
#include "esp_task_wdt.h"
#include "sd_card_player.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
//#include "minimp3_ex.h"

#define EXAMPLE_MAX_CHAR_SIZE    64
#define MOUNT_POINT "/sdcard"



#define PIN_NUM_MISO  (19)
#define PIN_NUM_MOSI  (23)
#define PIN_NUM_CLK   (18)
#define PIN_NUM_CS    (5)

//static i2s_chan_handle_t                rx_chan;        // I2S rx channel handler

///for speaker only
#define DUPLEX_GPIO_WS (14)
#define DUPLEX_GPIO_CLK (27)
#define DUPLEX_GPIO_DOUT (26)
#define DUPLEX_GPIO_DIN      (35)

i2s_chan_handle_t                tx_chan=NULL;        // I2S tx channel handler
i2s_chan_handle_t                rx_chan=NULL;        // I2S rx channel handler
TaskHandle_t playerHandle;
static const char *TAG = "example";
uint16_t currentFile=0;
uint16_t listLength=0;
char fileList[1000][16];
float volume = 4096;
const int NUM_FRAMES_TO_SEND = 256;
bool stop_playing = false;

void play_task(void *param);


void mp3Stop(void){
    stop_playing = true;
    vTaskDelay(10);
    vTaskDelete(playerHandle);
}

void mp3Start(void){
  stop_playing=false;
  xTaskCreatePinnedToCore(play_task, "task", 32768, NULL, 1, &playerHandle, 1);
}

void mp3Next(void){
    mp3Stop();
    currentFile++;
    if(currentFile>listLength)currentFile=0;
    mp3Start();

}
void mp3Prev(void){

    mp3Stop();
    if(currentFile>0)currentFile--;
    else currentFile=listLength;
    mp3Start();


}


void i2sDuplexStart(void)
{

/* Allocate a pair of I2S channel */
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
/* Allocate for TX and RX channel at the same time, then they will work in full-duplex mode */
i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);

/* Set the configurations for BOTH TWO channels, since TX and RX channel have to be same in full-duplex mode */
i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(8000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = DUPLEX_GPIO_CLK,
        .ws = DUPLEX_GPIO_WS,
        .dout = DUPLEX_GPIO_DOUT,
        .din = DUPLEX_GPIO_DIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};
i2s_channel_init_std_mode(tx_chan, &std_cfg);
i2s_channel_init_std_mode(rx_chan, &std_cfg);

i2s_channel_enable(tx_chan);
i2s_channel_enable(rx_chan);


}

void i2sChangeSampleRate(uint16_t sampleRate){

    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));

    i2s_std_clk_config_t clock_config=I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_chan,&clock_config));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(rx_chan,&clock_config));// microphone has to stay 32 bits
/*
    i2s_std_slot_config_t slot_config=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, I2S_SLOT_MODE_STEREO);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_chan,&slot_config));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(rx_chan,&slot_config));
*/
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}


///////////////////////////////////////////////////////////////////////////////////////////////////
const char* getExtension(const char * filename){
  const char* index =  strrchr(filename,'.');
//  ESP_LOGI(TAG,"extension:%s",index);
  return index;
}

void listDir(void){
  DIR *d;
  struct dirent *dir;
  d = opendir(MOUNT_POINT);
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      const char* ext = getExtension(dir->d_name);
      if(!strcmp(ext,".MP3")){
        strcpy(fileList[listLength],dir->d_name);
        ESP_LOGI(TAG,"filename listed: %s", fileList[listLength]);
        listLength++;
      }
    }
    closedir(d);
  }

}



void outputWrite(int16_t *samples, int frames)
{
    int32_t frames_buffer[2 * NUM_FRAMES_TO_SEND];
  // this will contain the prepared samples for sending to the I2S device
  int frame_index = 0;
  while (frame_index < frames)
  {
    // fill up the frames buffer with the next NUM_FRAMES_TO_SEND frames
    int frames_to_send = 0;
    for (int i = 0; i < NUM_FRAMES_TO_SEND && frame_index < frames; i++)
    {
      frames_buffer[i * 2] = (volume * samples[frame_index * 2]);
      frames_buffer[i * 2 + 1] = (volume * samples[frame_index * 2 + 1]);
      frames_to_send++;
      frame_index++;
    }
    // write data to the i2s peripheral - this will block until the data is sent
    size_t bytes_written = 0;
    i2s_channel_write(tx_chan, frames_buffer, frames_to_send* sizeof(int32_t) * 2, &bytes_written, portMAX_DELAY);
    if (bytes_written != frames_to_send * sizeof(int32_t) * 2)
    {
      ESP_LOGE(TAG, "Did not write all bytes:%d",bytes_written);
    }
  }
}
//typedef int (*MP3D_PROGRESS_CB)(void *user_data, size_t file_size, uint64_t offset, mp3dec_frame_info_t *info);
const int BUFFER_SIZE = 1024;

void play_task(void *param)
{
  short *pcm = (short *)malloc(sizeof(short) * MINIMP3_MAX_SAMPLES_PER_FRAME);
  uint8_t *input_buf = (uint8_t *)malloc(BUFFER_SIZE);

  if (!pcm)
  {
    ESP_LOGE("TASK", "Failed to allocate pcm memory");
  }
  if (!input_buf)
  {
    ESP_LOGE("TASK", "Failed to allocate input_buf memory");
  }
  while (true)
  {
    // mp3 decoder state
    mp3dec_t mp3d = {};
    mp3dec_init(&mp3d);
    mp3dec_frame_info_t info = {};
    // keep track of how much data we have buffered, need to read and decoded
    int to_read = BUFFER_SIZE;
    int buffered = 0;
    int decoded = 0;
    bool is_output_started = false;

//    if(!stop_playing){
//if(1){
    char filePath[32];
      sprintf(filePath,"%s/%s",MOUNT_POINT,fileList[currentFile]);
      ESP_LOGI("TASK","opening file: %s",filePath);
      FILE *fp = fopen(filePath, "rb");
      if (!fp)
      {
        ESP_LOGE("TASK", "Failed to open file: %s ",filePath);
        continue;
      }
      while (1)
      {
        // read in the data that is needed to top up the buffer
        size_t n = fread(input_buf + buffered, 1, to_read, fp);
        // feed the watchdog
        vTaskDelay(pdMS_TO_TICKS(1));
        buffered += n;
        if (buffered == 0)
        {
          // we've reached the end of the file and processed all the buffered data
          break;
        }
        // decode the next frame
        int samples = mp3dec_decode_frame(&mp3d, input_buf, buffered, pcm, &info);
        // we've processed this may bytes from teh buffered data
        buffered -= info.frame_bytes;
        // shift the remaining data to the front of the buffer
        memmove(input_buf, input_buf + info.frame_bytes, buffered);
        // we need to top up the buffer from the file
        to_read = info.frame_bytes;
        if (samples > 0)
        {
          if (!is_output_started)
          {
            i2sChangeSampleRate(info.hz);
            is_output_started = true;
          }

          // if we've decoded a frame of mono samples convert it to stereo by duplicating the left channel
          // we can do this in place as our samples buffer has enough space
          if (info.channels == 1)
          {
            for (int i = samples - 1; i >= 0; i--)
            {
              pcm[i * 2] = pcm[i];
              pcm[i * 2 - 1] = pcm[i];
            }
          }
  // write the decoded samples to the I2S output
          outputWrite(pcm,samples);
          // keep track of how many samples we've decoded
          decoded += samples;
        }
///THIS PAUSES THE MUSIC
        if(stop_playing){
          is_output_started=false;
          esp_task_wdt_reset();
          while(stop_playing){
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        }
      }
      ESP_LOGI("TASK", "Finished");

      is_output_started = false;
      fclose(fp);
      currentFile++;
      if(currentFile>listLength)currentFile=0;
    }
}


void startSDCard(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;

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
        .max_transfer_sz = 1000,
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
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

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

    listDir();
    i2sDuplexStart();
}
