#include "input.h"

#include <algorithm>
#include <esp_sleep.h>
#include <esp_system.h>
#include "game_save.h"
#include <Preferences.h>

static uint32_t s_lastInputUs = 0;
uint32_t lastPadState = 0xFFFFFFFF;
static const uint32_t INPUT_POLL_PERIOD_MS = 32;
constexpr int64_t INPUT_POLL_PERIOD_US = 1000 * INPUT_POLL_PERIOD_MS;
static share::BeforeRestartCallback s_beforeRestartCallback = nullptr;

// I2C joypad type
enum I2cPadType : uint8_t {
    I2C_PAD_NONE = 0,
    I2C_PAD_JOYV2,    // M5Stack JoyV2 at 0x63
    I2C_PAD_JOYV1_1,  // M5Stack Joystick v1.1 at 0x52
};
static I2cPadType s_i2cPadType = I2C_PAD_NONE;

static constexpr uint8_t JOYSTICK_CENTER   = 128;
static constexpr uint8_t JOYSTICK_DEADZONE = 25;
static constexpr int CARDPUTER_I2C_SCL = 1;
static constexpr int CARDPUTER_I2C_SDA = 2;

// JoyV2 (I2C 0x63) — register-based protocol
static constexpr uint8_t JOYSTICK2_ADDR                = 0x63;
static constexpr uint8_t JOYSTICK2_ADC_VALUE_8BITS_REG = 0x10;
static constexpr uint8_t JOYSTICK2_BUTTON_REG          = 0x20;

// Joystick v1.1 (I2C 0x52) — simple 3-byte read
static constexpr uint8_t JOYSTICK1_ADDR = 0x52;

namespace share
{
    void setBeforeRestartCallback(BeforeRestartCallback callback)
    {
        s_beforeRestartCallback = callback;
    }

    void clearBeforeRestartCallback()
    {
        s_beforeRestartCallback = nullptr;
    }

   bool shouldPollInput()
    {
        uint32_t now = esp_timer_get_time();
        if (now - s_lastInputUs < INPUT_POLL_PERIOD_US) {
            return false;
        }

        s_lastInputUs = now;
        return true;
    }

    static inline bool key(char c) {
        return M5Cardputer.Keyboard.isKeyPressed(c);
    }

    void checkCommonInput(const Keyboard_Class::KeysState& status)
    {
        // Bouton GO → restart (hack for quit game and reset memory)
        if (M5Cardputer.BtnA.pressedFor(1000)) {
            Preferences prefs; // Mark quit game flag in NVS
            prefs.begin("cardputer_emu", false);  // RW
            prefs.putBool("quit_game", true);
            prefs.end();

            if (s_beforeRestartCallback) {
                s_beforeRestartCallback();
            }
            
            while (gameIsSaving()) {
                delay(1); // wait for save to finish
            }
            
            esp_restart();
        }

        // Volume +
        if (key(CARDPUTER_VOL_UP_1) || (status.fn && key(CARDPUTER_VOL_UP_2))) {
            int v = M5Cardputer.Speaker.getVolume();
            M5Cardputer.Speaker.setVolume(std::min(v + 3, 255));
        }

        // Volume -
        if (key(CARDPUTER_VOL_DOWN_1) || (status.fn && key(CARDPUTER_VOL_DOWN_2))) {
            int v = M5Cardputer.Speaker.getVolume();
            M5Cardputer.Speaker.setVolume(std::max(v - 3, 0));
        }

        // Bright +
        if (key(CARDPUTER_BRIGHT_UP)) {
            int b = M5Cardputer.Display.getBrightness();
            M5Cardputer.Display.setBrightness(std::min(b + 2, 255));
        }

        // Bright - using cardputer adv UP/LEFT/ACTION at the same time 
        // will also decrease brightness, check for up not pressed
        if (key(CARDPUTER_BRIGHT_DOWN) && !key(CARDPUTER_UP_1)) {
            int b = M5Cardputer.Display.getBrightness();
            M5Cardputer.Display.setBrightness(std::max(b - 2, 0));
        }
    }

    void detectI2cPad()
    {
        Wire.begin(CARDPUTER_I2C_SDA, CARDPUTER_I2C_SCL);
        Wire.setClock(400000);

        // Try JoyV2 first (0x63)
        Wire.beginTransmission(JOYSTICK2_ADDR);
        if (Wire.endTransmission() == 0) {
            s_i2cPadType = I2C_PAD_JOYV2;
            printf("[INPUT] M5 Unit JoyV2 detected at 0x%02X (SDA=%d, SCL=%d)\n",
                   JOYSTICK2_ADDR, CARDPUTER_I2C_SDA, CARDPUTER_I2C_SCL);
            return;
        }

        // Then try Joystick v1.1 (0x52)
        Wire.beginTransmission(JOYSTICK1_ADDR);
        if (Wire.endTransmission() == 0) {
            s_i2cPadType = I2C_PAD_JOYV1_1;
            printf("[INPUT] M5 Unit Joystick v1.1 detected at 0x%02X (SDA=%d, SCL=%d)\n",
                   JOYSTICK1_ADDR, CARDPUTER_I2C_SDA, CARDPUTER_I2C_SCL);
            return;
        }

        s_i2cPadType = I2C_PAD_NONE;
        printf("[INPUT] No I2C joystick found (SDA=%d, SCL=%d)\n",
               CARDPUTER_I2C_SDA, CARDPUTER_I2C_SCL);
               
        Wire.end();
    }

    bool hasI2cPad()
    {
        return s_i2cPadType != I2C_PAD_NONE;
    }

    // JoyV2: read 2 bytes X/Y from register 0x10
    static bool joystick2_read_xy(uint8_t& x, uint8_t& y)
    {
        // Select 8bit register
        Wire.beginTransmission(JOYSTICK2_ADDR);
        Wire.write(JOYSTICK2_ADC_VALUE_8BITS_REG);
        if (Wire.endTransmission(false) != 0) {
            return false;
        }

        if (Wire.requestFrom(JOYSTICK2_ADDR, (uint8_t)2) != 2) {
            return false;
        }

        x = Wire.read();
        y = Wire.read();
        return true;
    }

    // JoyV2: read button from register 0x20
    static bool joystick2_read_button(uint8_t& btn)
    {
        Wire.beginTransmission(JOYSTICK2_ADDR);
        Wire.write(JOYSTICK2_BUTTON_REG);
        if (Wire.endTransmission(false) != 0) {
            return false;
        }

        if (Wire.requestFrom(JOYSTICK2_ADDR, (uint8_t)1) != 1) {
            return false;
        }

        btn = Wire.read();
        return true;
    }

    // Joystick v1.1: read 3 bytes (X, Y, BTN) in one shot
    static bool joystick1_read_all(uint8_t& x, uint8_t& y, uint8_t& btn)
    {
        if (Wire.requestFrom(JOYSTICK1_ADDR, (uint8_t)3) != 3) {
            return false;
        }

        x   = Wire.read();
        y   = Wire.read();
        btn = Wire.read();
        return true;
    }

    uint32_t pollI2cPad()
    {
        if (s_i2cPadType == I2C_PAD_NONE) {
            return 0;
        }

        uint8_t x8 = 0;
        uint8_t y8 = 0;
        uint8_t btnRaw = 1;  // default to "not pressed"

        if (s_i2cPadType == I2C_PAD_JOYV2) {
            if (!joystick2_read_xy(x8, y8)) return 0;
            joystick2_read_button(btnRaw);
        } else {  // I2C_PAD_JOYV1_1
            if (!joystick1_read_all(x8, y8, btnRaw)) return 0;
            x8 = 255 - x8;  // v1.1 X axis is inverted relative to JoyV2
        }

        uint32_t state = 0;

        // Axes (same range 0-255, center ~128 for both devices)
        if (x8 < (JOYSTICK_CENTER - JOYSTICK_DEADZONE)) {
            state |= PAD_RIGHT;
        } else if (x8 > (JOYSTICK_CENTER + JOYSTICK_DEADZONE)) {
            state |= PAD_LEFT;
        }

        if (y8 < (JOYSTICK_CENTER - JOYSTICK_DEADZONE)) {
            state |= PAD_DOWN;
        } else if (y8 > (JOYSTICK_CENTER + JOYSTICK_DEADZONE)) {
            state |= PAD_UP;
        }

        // Button A (0x00 = pressed for both devices)
        if (btnRaw == 0x00) {
            state |= PAD_A;
        }

        return state;
    }
}
