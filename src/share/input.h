#pragma once
#include <M5Cardputer.h>

#define CARDPUTER_LEFT_1       'a'
#define CARDPUTER_LEFT_2       ','

#define CARDPUTER_RIGHT_1      'd'
#define CARDPUTER_RIGHT_2      '/'

#define CARDPUTER_UP_1         'e'
#define CARDPUTER_UP_2         ';'

#define CARDPUTER_DOWN_1       's'
#define CARDPUTER_DOWN_2       '.'
#define CARDPUTER_DOWN_3       'z'

#define CARDPUTER_BTN_A_1      'l'
#define CARDPUTER_BTN_A_2      'j'

#define CARDPUTER_BTN_B        'k'

#define CARDPUTER_BTN_START    '1'
#define CARDPUTER_BTN_SELECT   '2'

#define CARDPUTER_SCREEN_TOGGLE '\\'

#define CARDPUTER_ZOOM_PLUS    '/' // FN + arrow right
#define CARDPUTER_ZOOM_MINUS   ',' // FN + arrow left

#define CARDPUTER_VOL_UP_1          '='     // Volume +
#define CARDPUTER_VOL_UP_2          ';'     // FN + arrow up

#define CARDPUTER_VOL_DOWN_1        '-'     // Volume -
#define CARDPUTER_VOL_DOWN_2        '.'     // FN + arrow down

#define CARDPUTER_BRIGHT_UP         ']'     // Bright +
#define CARDPUTER_BRIGHT_DOWN       '['     // Bright -

extern uint32_t lastPadState;

namespace share
{
    typedef void (*BeforeRestartCallback)();

    bool shouldPollInput(); 
    void checkCommonInput(const Keyboard_Class::KeysState& status);
    void setBeforeRestartCallback(BeforeRestartCallback callback);
    void clearBeforeRestartCallback();

    // I2C PAD (M5Stack JoyV2 or Joystick v1.1)
    void detectI2cPad();
    bool hasI2cPad();
    uint32_t pollI2cPad();

    // Bitmask
    enum PadBits : uint32_t {
        PAD_UP      = 1u << 0,
        PAD_DOWN    = 1u << 1,
        PAD_LEFT    = 1u << 2,
        PAD_RIGHT   = 1u << 3,
        PAD_A       = 1u << 4,
        PAD_B       = 1u << 5,
        PAD_START   = 1u << 6,
        PAD_SELECT  = 1u << 7,
    };
}
