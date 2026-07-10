#include <cassert>
#include <string>

#include "share/utf8_text.h"

int main() {
    assert(share::truncateUtf8("Tetris.gb", 20) == "Tetris.gb");
    assert(share::utf8CharacterCount("宝可梦.gbc") == 7);
    assert(share::truncateUtf8("宝可梦红绿蓝黄.gbc", 10) == "宝可梦...gbc");

    std::string malformed = "A";
    malformed.push_back(static_cast<char>(0xE4));
    malformed += "B";
    assert(share::utf8CharacterCount(malformed) == 3);
    assert(share::truncateUtf8(malformed, 2) == "..");
}
