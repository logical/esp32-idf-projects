#include "driver/gpio.h"
#include "pinout.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "components/ssd1306/ssd1306.h"
#include "components/ssd1306/font8x8_basic.h"

extern 	SSD1306_t dev;

int8_t readKeypad(void){


    int8_t key=-1;


    gpio_set_level(KEYPAD_OUTPUT1,0);
    if(!gpio_get_level(KEYPAD_INPUT1))key=1;
    if(!gpio_get_level(KEYPAD_INPUT2))key=2;
    if(!gpio_get_level(KEYPAD_INPUT3))key=3;
    gpio_set_level(KEYPAD_OUTPUT1,1);
    if (key>-1)return key;

    gpio_set_level(KEYPAD_OUTPUT2,0);
    if(!gpio_get_level(KEYPAD_INPUT1))key=4;
    if(!gpio_get_level(KEYPAD_INPUT2))key=5;
    if(!gpio_get_level(KEYPAD_INPUT3))key=6;
    gpio_set_level(KEYPAD_OUTPUT2,1);
    if (key>-1)return key;

    gpio_set_level(KEYPAD_OUTPUT3,0);
    if(!gpio_get_level(KEYPAD_INPUT1))key=7;
    if(!gpio_get_level(KEYPAD_INPUT2))key=8;
    if(!gpio_get_level(KEYPAD_INPUT3))key=9;
    gpio_set_level(KEYPAD_OUTPUT3,1);
    if (key>-1)return key;

    gpio_set_level(KEYPAD_OUTPUT4,0);
    if(!gpio_get_level(KEYPAD_INPUT1))key=10;//*
    if(!gpio_get_level(KEYPAD_INPUT2))key=0;
    if(!gpio_get_level(KEYPAD_INPUT3))key=11;//#
    gpio_set_level(KEYPAD_OUTPUT4,1);
    return key;

}


void keypad_task(void *param){

    while(1){
        int i;
        char c=' ';
       i=readKeypad();
//        ESP_LOGI("TAG","keypad %d",i);
       if(i>-1){
           switch(i){
               case 1:
                   break;
               case 2:
                   break;
               case 3:
                   break;
               case 4:
                   break;
               case 5:
                   break;
               case 6:
                   break;
               case 7:
                   break;
               case 8:
                   break;
               case 9:
                   break;
               case 0:
                   break;
               case 10:
                   break;
               case 11:
                   break;
               default:
                   break;
           }

            itoa(i,&c,16);
            ssd1306_display_text(&dev, 0, &c, 1, false);
       }

	vTaskDelay(1000 / portTICK_PERIOD_MS);

    ssd1306_clear_screen(&dev,false);
    }
}

    TaskHandle_t xHandle = NULL;

void initKeypad(void){

    gpio_set_level(KEYPAD_INPUT1,1);
    gpio_set_level(KEYPAD_INPUT2,1);
    gpio_set_level(KEYPAD_INPUT3,1);

    gpio_set_direction(KEYPAD_INPUT1, GPIO_MODE_INPUT);
    gpio_set_direction(KEYPAD_INPUT2, GPIO_MODE_INPUT);
    gpio_set_direction(KEYPAD_INPUT3, GPIO_MODE_INPUT);
    //inputs need pull resistor or they float
    gpio_pulldown_dis(KEYPAD_INPUT1);
    gpio_pulldown_dis(KEYPAD_INPUT2);
    gpio_pulldown_dis(KEYPAD_INPUT3);

    gpio_pullup_en(KEYPAD_INPUT1);
    gpio_pullup_en(KEYPAD_INPUT2);
    gpio_pullup_en(KEYPAD_INPUT3);

    gpio_set_direction(KEYPAD_OUTPUT1, GPIO_MODE_OUTPUT);
    gpio_set_direction(KEYPAD_OUTPUT2, GPIO_MODE_OUTPUT);
    gpio_set_direction(KEYPAD_OUTPUT3, GPIO_MODE_OUTPUT);
    gpio_set_direction(KEYPAD_OUTPUT4, GPIO_MODE_OUTPUT);

    gpio_set_level(KEYPAD_OUTPUT1,1);
    gpio_set_level(KEYPAD_OUTPUT2,1);
    gpio_set_level(KEYPAD_OUTPUT3,1);
    gpio_set_level(KEYPAD_OUTPUT4,1);

xTaskCreate( keypad_task, "KEYPAD", 32768, NULL, 1, &xHandle );


}

