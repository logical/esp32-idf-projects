#ifndef __SD_CARD_PLAYER__
#define __SD_CARD_PLAYER__

#include "driver/i2s_std.h"




    void startSDCard(void);
    void listDir(void);

    void mp3Start(void);
    void mp3Stop(void);
    void mp3Next(void);
    void mp3Prev(void);
    void i2sOutputStart(uint16_t sampleRate);
    void i2sInputStart(void);
    void i2sChangeSampleRate(uint16_t sampleRate);



#endif
