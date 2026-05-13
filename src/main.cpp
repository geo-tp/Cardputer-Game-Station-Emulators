#include <M5Cardputer.h>
#include <select_rom.h>
#include "cardputer/CardputerView.h"
#include "cardputer/CardputerInput.h"
#include "cardputer/SdService.h"
#include "vfs/vfs_xip.h"
#include "vfs/rom_flash_io.h"
#include "vfs/rom_xip.h"
#include "vfs/partitioner.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "nes/run_nes.h"
#include "sms/run_sms.h"
#include "ngp/run_ngp.h"
#include "ws/run_ws.h"
#include "pce/run_pce.h"
#include "lynx/run_lynx.h"
#include "genesis/run_genesis.h"
#include "gbc/run_gbc.h"
#include "msx/run_msx.h"
#include "snes/run_snes.h"
#include "atari7800/run_a7800.h"
#include "atari2600/run_a2600.h"
#include "gx4000/run_gx4000.h"
#include "last_game.h"
#define RETRO_COMPAT_IMPLEMENTATION
#include "ngp/race/retro_compat.h"
#include "esp_task_wdt.h"
#include "share/input.h"
#include "share/emu_log_cpp.h"

#if defined(CONFIG_BT_ENABLED)
extern "C" bool btInUse(void) {
  return false;
}
#endif

void setup() {
  // Set high priority for the current task (where the emulator will run)
  vTaskPrioritySet(NULL, 19);

  // Copied from Gameboy Enhanced Firmware setup
#ifdef DISABLE_WATCHDOGS
  M5.Log.printf("Disabling all WatchDogs...\n");
  esp_task_wdt_deinit(); // fully disables and removes TWDT
  disableCore0WDT(); // disable WDT
  disableCore1WDT(); // disable WDT
  esp_task_wdt_delete(NULL); // disable WDT on this therad - legacy
#endif

  auto cfg = M5.config();
  cfg.output_power = true;
  M5Cardputer.begin(cfg);
  CardputerInput input;
  SdService sd;
  CardputerView display;
  display.initialize();

  // SD
  while (!sd.begin()) {
    display.topBar("SD CARD FOR ROMS", false, false);
    display.subMessage("No SD card found", 1000);
    display.subMessage("Insert SD card", 0);
  }

  std::string romPath;
  auto romFolder = getRomFolderFromNvs(display, input, sd);
  romFolder = romFolder.empty() ? "/" : romFolder;

  if (isQuittingGame()) {
    romPath = getRomPath(sd, display, input, romFolder, true);
  } else {
    // Welcome
    display.welcome();
    input.waitPress(4000);

    // Try to get last game from NVS or select a new one
    romPath = getLastGameFromNvs(display, input, sd);
    if (romPath.empty()) {
      auto skipWelcome = romFolder == "/" ? false : true;
      romPath = getRomPath(sd, display, input, romFolder, skipWelcome);
    } else {
      romPath = "/sd" + romPath; // ensure sd prefix
    }
  }
  EMU_LOG("Selected ROM: %s\n", romPath.c_str());

  display.topBar("COPYING ROM TO FLASH", false, false);
  display.subMessage("Loading...", 0);
  
  // Find the rom partition (SPIFFS)
  const esp_partition_t* romPart = findRomPartition("spiffs");
  if (!romPart) {
    while (1) {
      display.topBar("ERROR", false, false);
      display.subMessage("No ROM partition", 0);
      delay(1500);
    }
  }

  // Copy the ROM file to the partition
  size_t romSize = 0;
  if (!copyFileToPartition(romPath.c_str(), romPart, &romSize, CardputerView::copyProgress, &display)) {
    // User is using the launcher
    if (isLauncherLayout()) {
      // Ask to flash the launcher Game Station partition to unlock full size
      ConfirmationSelector confirm(display, input);
      bool confirmed = confirm.select("ROM IS TOO HEAVY", "Change to 4MB layout?");
      
      if (confirmed) {
        display.topBar("FLASHING PARTITIONS", false, false);
        display.subMessage("Allow up to 4MB roms", 3000);
        auto ok = flashGameStationPartition();
        if (ok) {
          display.subMessage("Success, rebooting...", 3000);
          saveLastGameToNvs(romPath);
          esp_restart();
        } else {
          display.subMessage("Partition flashing failed", 3000);
        }
      }
    }
    // Rom limit is reached (either launcher default 1MB/4MB or normal 5.5MB)
    while (1) {
        display.topBar("ROM IS TOO HEAVY", false, false);
        display.subMessage("Copy ROM to flash failed", 1500);
        display.subMessage("ROM limit is reached", 1500);
        delay(1500);
    }
  }

  input.flushInput(10); // flush any input just in case

  // Map the ROM partition in XIP
  if (xip_map_rom_partition("spiffs", romSize) != 0) {
    while (1) {
      display.topBar("ERROR", false, false);
      display.subMessage("Map ROM failed", 0);
      delay(1500);
    }
  }
  // Register the XIP VFS
  vfs_xip_register();
  // Check the extension to choose the emulator
  auto ext = getRomType(romPath);

  // Show keymapping
  display.topBar("- + SOUND [ ] BRIGHT", false, false);
  int numButtons = (ext == ROM_TYPE_GENESIS) ? 3 : 2;
  numButtons = (ext == ROM_TYPE_SNES) ? 6 : numButtons;
  display.showKeymapping(numButtons);

  // Wait for key press or show tips
  uint32_t lastUpdate = millis();
  int state = 0;
  for (;;) {
    char key = input.readChar();
    if (key != KEY_NONE) break;

    // Show tips
    uint32_t now = millis();
    if (now - lastUpdate >= 2000) {
      lastUpdate = now;
      switch (state) {
        case 0: display.topBar("PRESS ANY KEY TO START", false, false); break;
        case 1: display.topBar("KEY \\ SCREEN MODE",       false, false); break;
        case 2: display.topBar("G0 1SEC TO QUIT GAME",     false, false); break;
        case 3: display.topBar("FN + ARROWS FOR ZOOM",     false, false); break;
        case 4: display.topBar("- + SOUND [ ] BRIGHT",     false, false); break;
      }
      state = (state + 1) % 5;
    }
    delay(1);
  }

  // Save last game to nvs
  if (ext != ROM_TYPE_UNKNOWN) {
      saveLastGameToNvs(romPath);
  }

  // Prepare rom filename for emulators
  auto pos = romPath.find_last_of("/\\");
  std::string romName = (pos == std::string::npos) ? romPath : romPath.substr(pos + 1);

  // Initialize I2C M5Stack JoyV2 if any
  share::detectI2cPad();
  
  EMU_LOG("HEAP BEFORE EMU: %u bytes\n", esp_get_free_heap_size());
  EMU_LOG("MAX BLOCK BEFORE EMU: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  // Run the emulator
  if (ext == ROM_TYPE_NES) {
      // --- NES ---
      romName = "/xip/" + romName;
      run_nes(romName.c_str());
  }
  else if (ext == ROM_TYPE_GAMEGEAR || ext == ROM_TYPE_SMS || ext == ROM_TYPE_SG1000 || ext == ROM_TYPE_COLECO) {
      // --- SMS family (SMS / GG / SG-1000 / ColecoVision) ---
      SmsConsoleMode mode = SMS_MODE_SMS;
      if (ext == ROM_TYPE_GAMEGEAR) mode = SMS_MODE_GG;
      else if (ext == ROM_TYPE_SG1000) mode = SMS_MODE_SG1000;
      else if (ext == ROM_TYPE_COLECO) mode = SMS_MODE_COLECO;
      run_sms(get_rom_ptr(), get_rom_size(), mode, romName.c_str());
  }
  else if (ext == ROM_TYPE_NGP) {
      // --- Neo Geo Pocket / Color ---
      int machine = detectNeoGeoPocketFromRom(get_rom_ptr(), get_rom_size(), romPath);
      run_ngp(get_rom_ptr(), get_rom_size(), romName.c_str(), machine);
  }
  else if (ext == ROM_TYPE_GENESIS) {
      // --- Megadrive / Genesis ---
      run_genesis(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_WS) {
      // --- WonderSwan / Color ---
      run_ws(get_rom_ptr(), get_rom_size(), romName.c_str(), detectWonderSwanFromRom(romPath));
  }
  else if (ext == ROM_TYPE_PCE) {
      // --- PC Engine / TurboGrafx-16 ---
      run_pce(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_GB) { 
      // --- Game Boy / Color ---
      run_gbc(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_LYNX) {
      // --- Lynx ---
      run_lynx(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_SNES) {
      // --- SNES / Super Famicom ---
      display.displaySnesInfo();
      input.waitPress();
      run_snes(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_MSX) {
      // --- MSX ---
      display.displayMsxInfo();
      input.waitPress();
      run_msx(get_rom_ptr(), get_rom_size(), romName.c_str(), romPath.c_str());
  }
  else if (ext == ROM_TYPE_ATARI7800) {
      // --- Atari 7800 ---
      run_a7800(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_ATARI2600) {
      // --- Atari 2600 ---
      run_a2600(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
    else if (ext == ROM_TYPE_GX4000) {
      // --- Amstrad GX4000 ---
      run_gx4000(get_rom_ptr(), get_rom_size(), romName.c_str());
    }
  else {
      display.topBar("ERROR", false, false);
      display.subMessage("Unsupported ROM type", 0);
      while (1) delay(1000);
  }
}

void loop() {
  /* run_emulator is blocking */
}
