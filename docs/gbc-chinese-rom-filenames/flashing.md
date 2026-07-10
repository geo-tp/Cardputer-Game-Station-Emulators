# Flashing the Chinese ROM-name firmware

The build uses a revised 8 MB partition table:

- Application: 3 MB
- ROM storage (SPIFFS): 4.94 MB

Use the complete flash image when upgrading from the stock 2.5 MB application
layout. It includes the bootloader, partition table, and firmware, and must be
written at address `0x0`.

```sh
esptool.py --chip esp32s3 write_flash 0x0 cardputer-game-station-chinese-full.bin
```

Flashing the full image replaces the device partition table. Back up any data
stored in flash before doing so. ROMs and saves on the SD card are unaffected.

For an existing device already using this 3 MB application layout, write only
the application image at `0x10000`:

```sh
esptool.py --chip esp32s3 write_flash 0x10000 firmware-chinese-rom-names.bin
```
