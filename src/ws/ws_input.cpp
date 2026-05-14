#include "ws_input.h"
#include <M5Cardputer.h>
#include <stdint.h>
#include "ws_input.h"
#include "share/input.h"
#include "ws_save.h"
#include "ws_state.h"

extern bool ws_fullscreen;
extern int  ws_zoomPercent;
extern uint32_t lastPadState;

extern "C" int ws_input_poll(int mode)
{
  M5Cardputer.update();
  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
  uint16_t state = 0;
  static bool quitFlushDone = false;
  static bool stateSaveLatch = false;
  static bool stateLoadLatch = false;

  if (M5Cardputer.BtnA.pressedFor(1000) && !quitFlushDone) {
    ws_save_force_flush();
    quitFlushDone = true;
  }

  share::checkCommonInput(status);

  const bool saveStateCombo = status.fn && M5Cardputer.Keyboard.isKeyPressed('s');
  const bool loadStateCombo = status.fn && M5Cardputer.Keyboard.isKeyPressed('l');
  if (saveStateCombo && !stateSaveLatch) {
    ws_state_request_save();
    stateSaveLatch = true;
  } else if (!saveStateCombo) {
    stateSaveLatch = false;
  }
  if (loadStateCombo && !stateLoadLatch) {
    ws_state_request_load();
    stateLoadLatch = true;
  } else if (!loadStateCombo) {
    stateLoadLatch = false;
  }
  if (saveStateCombo || loadStateCombo) {
    lastPadState = state;
    return (int)state;
  }

  // I2C PAD (M5Stack JoyV2)
  if (share::hasI2cPad()) {
      int i2cPad = share::pollI2cPad();
      if (i2cPad & share::PAD_LEFT)  state |= WS_X4; 
      if (i2cPad & share::PAD_RIGHT) state |= WS_X2; 
      if (i2cPad & share::PAD_UP)    state |= WS_X1; 
      if (i2cPad & share::PAD_DOWN)  state |= WS_X3;
      if (i2cPad & share::PAD_A)     state |= WS_A;
  }

  // Directional pad
  const bool left  = M5Cardputer.Keyboard.isKeyPressed('e');
  const bool right = M5Cardputer.Keyboard.isKeyPressed('z') || M5Cardputer.Keyboard.isKeyPressed('s');
  const bool up    = M5Cardputer.Keyboard.isKeyPressed('d');
  const bool down  = M5Cardputer.Keyboard.isKeyPressed('a');

  // mode == 0 : horizontal
  if (mode == 0) {
    if (left)  state |= WS_X1;
    if (up)    state |= WS_X2;
    if (right) state |= WS_X3;
    if (down)  state |= WS_X4;
  // mode == 1 : vertical 
  } else {
    if (left)  state |= WS_Y1;
    if (up)    state |= WS_Y2;
    if (right) state |= WS_Y3;
    if (down)  state |= WS_Y4;
  }

  // Secondary directional pad 
  const bool a = M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_UP_2);
  const bool w = M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_DOWN_2);
  const bool d = M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_RIGHT_2);
  const bool s = M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_LEFT_2);

  if (mode == 0) {
    // In horizontal mode, WASD -> Y cross
    if (a) state |= WS_Y1;
    if (w) state |= WS_Y2;
    if (d) state |= WS_Y3;
    if (s) state |= WS_Y4;
  } else {
    // In vertical mode, WASD -> X cross
    if (a) state |= WS_X1;
    if (w || M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_B)) state |= WS_X2;
    if (d || M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_A_1)) state |= WS_X3;
    if (s) state |= WS_X4;
  }

  //  Boutons
  if (M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_A_1) && mode == 0) state |= WS_A;       // A
  if (M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_A_2) && mode == 0) state |= WS_A;       // A
  if (M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_B) && mode == 0) state |= WS_B;       // B

  if (M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_START)) state |= WS_START;  // START 
  if (M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_BTN_SELECT))  state |= WS_OPTION; // OPTION 

  // Zoom / fullscreen toggle
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_SCREEN_TOGGLE)) {
    if (!ws_fullscreen) {
      ws_fullscreen  = true;
      ws_zoomPercent = 100;
    } else {
      ws_zoomPercent += 10;
      if (ws_zoomPercent > 150) {
        ws_zoomPercent = 100;
        ws_fullscreen  = false;
      }
    }
    return (int)state;
  }

  if (status.fn && M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_ZOOM_PLUS)) {
    ws_zoomPercent = std::min(150, ws_zoomPercent + 1);
    return (int)state;
  }

  if (status.fn && M5Cardputer.Keyboard.isKeyPressed(CARDPUTER_ZOOM_MINUS)) {
    ws_zoomPercent = std::max(100, ws_zoomPercent - 1);
    return (int)state;
  }
  
  lastPadState = state;
  return (int)state;
}
