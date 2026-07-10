# Chinese ROM filename rendering

## Problem

The Cardputer ROM browser renders all filenames with the ASCII-only `Font0`.
UTF-8 Chinese filenames therefore appear as mojibake or missing glyphs while
choosing GB/GBC ROMs (and the same issue can affect any supported ROM type).

## Scope

- Render UTF-8 Chinese characters in the ROM list and selected-row marquee.
- Keep ASCII filenames, extensions, colours, navigation, and ROM selection
  unchanged.
- Do not change filename bytes or path handling; the SD filename remains the
  source of truth used to open the ROM.

## Acceptance criteria

1. A UTF-8 Simplified-Chinese GB or GBC filename is readable in the list and
   while it scrolls as the selected row.
2. Truncation never cuts a UTF-8 character in half.
3. ASCII filenames retain their existing visible behaviour.
4. The resulting firmware fits its 8 MB flash partition table without
   overwriting the ROM storage partition.
