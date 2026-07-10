# Test plan

## TDD cases

- ASCII shorter than the limit is unchanged.
- Chinese text is counted by characters rather than UTF-8 bytes.
- Mixed Chinese/ASCII text truncates on character boundaries and preserves the
  filename suffix.
- Invalid leading bytes are treated as individual bytes and do not cause a
  loop or out-of-range access.

## Verification commands

```sh
g++ -std=c++17 -Isrc test/test_utf8_text.cpp -o /tmp/test_utf8_text && /tmp/test_utf8_text
pio run -e m5stack-stamps3
pio run -e m5stack-stamps3 -t package
```

## Resolved build blocker

`src/msx/fMSX/MSX.c` previously selected Neo Geo Pocket's unrelated
case-insensitive `Sound.h` on this build's include path. It now includes
`../EMULib/Sound.h` explicitly. A complete PlatformIO build validates that
the change resolves the blocker, verifies the firmware fits in the revised
3 MB application partition, and produces a flashable binary.
