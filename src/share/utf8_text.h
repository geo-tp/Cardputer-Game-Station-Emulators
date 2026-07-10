#pragma once

#include <cstddef>
#include <string>

namespace share {

inline size_t utf8CodePointLength(const std::string& text, size_t offset) {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    size_t length = 1;

    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;

    if (offset + length > text.size()) return 1;
    for (size_t i = 1; i < length; ++i) {
        if ((static_cast<unsigned char>(text[offset + i]) & 0xC0) != 0x80) return 1;
    }
    return length;
}

inline size_t utf8ByteOffset(const std::string& text, size_t characterCount) {
    size_t offset = 0;
    while (offset < text.size() && characterCount-- > 0) {
        offset += utf8CodePointLength(text, offset);
    }
    return offset;
}

inline size_t utf8CharacterCount(const std::string& text) {
    size_t count = 0;
    for (size_t offset = 0; offset < text.size(); ++count) {
        offset += utf8CodePointLength(text, offset);
    }
    return count;
}

inline std::string truncateUtf8(const std::string& text, size_t maxCharacters) {
    const size_t characterCount = utf8CharacterCount(text);
    if (characterCount <= maxCharacters) return text;

    constexpr const char* ellipsis = "...";
    constexpr size_t ellipsisLength = 3;
    if (maxCharacters <= ellipsisLength) return std::string(ellipsis, maxCharacters);

    const size_t halfLength = (maxCharacters - ellipsisLength) / 2;
    const size_t firstEnd = utf8ByteOffset(text, halfLength);
    const size_t lastStart = utf8ByteOffset(text, characterCount - halfLength);
    return text.substr(0, firstEnd) + ellipsis + text.substr(lastStart);
}

} // namespace share
