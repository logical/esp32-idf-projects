#ifndef __BT_APP_PINOUT_H__
#define __BT_APP_PINOUT_H__
/////////////////////////////////////
// DISPLAY and fm tx
////////////////////////////////////
#define  CONFIG_SDA_GPIO 21
#define  CONFIG_SCL_GPIO 22
#define  CONFIG_RESET_GPIO -1


/////////////SD CARD
#define PIN_NUM_MISO  (19)
#define PIN_NUM_MOSI  (23)
#define PIN_NUM_CLK   (18)
#define PIN_NUM_CS    (5)

//static i2s_chan_handle_t                rx_chan;        // I2S rx channel handler

#define DUPLEX_GPIO_WS (14)
#define DUPLEX_GPIO_CLK (27)
#define DUPLEX_GPIO_DOUT (26)
#define DUPLEX_GPIO_DIN      (35)


/////////////////////////////////////
// 12 button keypad
#define KEYPAD_INPUT1 (25)
#define KEYPAD_INPUT2 (33)
#define KEYPAD_INPUT3 (32)
#define KEYPAD_OUTPUT1 (13)
#define KEYPAD_OUTPUT2 (16)
#define KEYPAD_OUTPUT3  (17)
#define KEYPAD_OUTPUT4  (4)

#endif
