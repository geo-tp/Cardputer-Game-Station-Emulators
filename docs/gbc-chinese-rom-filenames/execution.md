# Execution plan

1. Add a small, platform-independent UTF-8 truncation helper.
2. Cover its ASCII, Chinese, mixed-text, and malformed-byte behaviour with a
   host-side C++ test.
3. Use M5GFX's bundled Simplified-Chinese `efontCN_10`, scaled to the existing
   list height, only while drawing ROM filenames and the selected-row marquee;
   then restore the normal UI font.
4. Run the host test and the configured firmware build. Record unrelated
   compiler failures if they prevent a full build.
5. Fix the build-blocking fMSX include ambiguity so the requested firmware
   image can be produced.
6. Resize the factory application partition from 2.5 MB to 3 MB and preserve
   4.94 MB for ROM storage, so the Chinese font fits safely on the 8 MB device.

## Decisions

- `efontCN_10` is provided by the existing M5GFX dependency, so the change
  does not add a new library or an external font asset. It keeps the firmware
  footprint lower than the 16-pixel variant while retaining list readability.
- The feature applies to all ROM filenames because the browser is shared;
  GB/GBC needs no emulator-specific encoding conversion.
- `MSX.c` now includes its sibling EMULib `Sound.h` explicitly. On
  case-insensitive filesystems, the previous generic include resolved to
  Neo Geo Pocket's unrelated `Sound.h` first, leaving the MSX sound symbols
  undefined.
- The project advertises support for ROMs up to 4 MB. The revised 4.94 MB
  SPIFFS partition preserves that capability while providing enough application
  space for the Chinese font.
