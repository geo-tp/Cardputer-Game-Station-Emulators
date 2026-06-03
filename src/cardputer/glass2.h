// glass2.h — optional M5Stack Glass2 secondary display
// 1.51" transparent OLED, SSD1309 driver, 128x64, I2C 0x3C
// Grove port: SDA=GPIO2, SCL=GPIO1 (Cardputer ADV)
//
// Initialisation is runtime-detected: if no Glass2 is connected,
// glass2Init() returns false and all subsequent calls are no-ops.

#pragma once

#include <M5UnitGLASS2.h>

#define GLASS2_SDA 2
#define GLASS2_SCL 1

static M5UnitGLASS2 _g2(GLASS2_SDA, GLASS2_SCL);
static bool _g2_ready = false;

inline void glass2Init() {
    _g2_ready = _g2.init();
    if (_g2_ready) {
        _g2.fillScreen(TFT_BLACK);
        _g2.setTextColor(TFT_WHITE, TFT_BLACK);
    }
}

// Show up to 2 lines. Pass nullptr to leave a line blank.
// Line 1 at y=8 (context / system), line 2 at y=40 (rom name).
inline void glass2Show(const char* l1, const char* l2 = nullptr) {
    if (!_g2_ready) return;
    _g2.fillScreen(TFT_BLACK);
    _g2.setTextSize(1);
    _g2.setTextColor(TFT_WHITE, TFT_BLACK);
    if (l1) { _g2.setCursor(0,  8); _g2.print(l1); }
    if (l2) { _g2.setCursor(0, 40); _g2.print(l2); }
}

inline void glass2Clear() {
    if (!_g2_ready) return;
    _g2.fillScreen(TFT_BLACK);
}
