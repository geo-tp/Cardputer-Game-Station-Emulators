#include "snes_rom.h"

#include <string.h>
#include <ctype.h>

// List of games that are known to have issues with line-by-line rendering, 
// and thus use an alternate rendering method
static const char* const kAltGameTitles[] =
{
    "F-ZERO",
    "SUPER MARIOKART",
    "SUPER MARIO KART",
    "HYPER ZONE",
    "SUPER CASTLEVANIA 4",
    "SUPER GHOULS'N GHOSTS",
    "CARRIER ACES",
    "FINAL FIGHT",
    "FINAL FIGHT 2",
    "FINAL FIGHT 3",
    "FINAL FIGHT GUY",
    "FF MYSTIC QUEST",
    "DEMON'S CREST",
    "SUPER BOMBERMAN",
    "ALADDIN",
    "THE NINJAWARRIORS",
    "STREET FIGHTER 2",
    "STREET FIGHTER2 TURBO",
    "SUPER PUNCH-OUT!!",
    "DD5 THE SHADOW FALLS",
    "JURASSIC PARK",
    "THE LOST VIKINGS",
    "THE LOST VIKINGS II",
    "T.M.N.T.5",
    "WILD GUNS",
    "R-TYPE 3",
    "THE MAGICAL QUEST",
    "KNIGHTS OF THE ROUND",
    "LEMMINGS",
    "LEMMINGS 2,THE TRIBES",
    "POWER RANGERS MOVIE",
    "SAILOR MOON",
    "DRAGONBALL Z 2",
    "F1 POLE POSITION",
    "FATAL FURY",
    "FATAL FURY2",
    "FATAL FURY SPECIAL",
    "TOP GEAR",
    "TOP GEAR 2",
    "KAWASAKI SUPERBIKE CH",
    "STREET RACER",
    "MANSELL",
    "CAPTAIN COMMANDO",
    "BT IN BATTLEMANIACS",
    "RIVAL TURF",
    "BRAWL BROTHERS",
    "ALIEN vs. PREDATOR",
    "WAR OF THE GEMS",
    "POWER RANGERS",
    "ROBOCOP VS THE TERMIN",
    "COMBATRIBES",
    "TAZ-MANIA",
    "X-MEN",
    "SPAWN",
    "WOLVERINE RAGE",
    "X-KALIBER 2097",
    "CHRONO TRIGGER",
    "CYBERNATOR",
    "POCKY ROCKY",
    "ARDY LIGHTFOOT",
    "DARK WATER",
    "DRAGON BRUCE LEE",
    "CHUCK ROCK",
    "INDIANA JONES GREAT",
    "JUSTICE LEAGUE",
    "INDY CAR CHALLENGE",
    "ROAD RIOT 4WD",
    "RR DEATH VALLEY RALLY",
    "Speed Racer",
    "SHAQ FU"
};

static bool streq_nocase(const char* a, const char* b)
{
    if (!a || !b)
        return false;

    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        ++a;
        ++b;
    }

    return (*a == '\0' && *b == '\0');
}

static bool titleLooksAltGame(const char* title)
{
    if (!title || !*title)
        return false;

    for (size_t i = 0; i < sizeof(kAltGameTitles) / sizeof(kAltGameTitles[0]); ++i)
    {
        if (streq_nocase(title, kAltGameTitles[i]))
            return true;
    }

    return false;
}

static void trim_right(char* s)
{
    if (!s)
        return;

    size_t len = strlen(s);
    while (len > 0)
    {
        const char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            s[len - 1] = '\0';
            --len;
        }
        else
        {
            break;
        }
    }
}

static void read_snes_title_at(char* out,
                               size_t outSize,
                               const uint8_t* rom,
                               size_t romSize,
                               size_t headerOffset)
{
    if (!out || outSize == 0)
        return;

    out[0] = '\0';

    if (!rom)
        return;

    /* titre interne SNES = 21 octets */
    if (romSize < headerOffset + 21)
        return;

    size_t n = 21;
    if (n >= outSize)
        n = outSize - 1;

    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char c = rom[headerOffset + i];
        out[i] = (c >= 32 && c <= 126) ? (char)c : ' ';
    }

    out[n] = '\0';
    trim_right(out);
}

bool snes_read_title(char* out, size_t outSize, const uint8_t* rom, size_t romSize)
{
    if (!out || outSize == 0)
        return false;

    out[0] = '\0';

    read_snes_title_at(out, outSize, rom, romSize, 0x7FC0);
    if (out[0] != '\0')
        return true;

    read_snes_title_at(out, outSize, rom, romSize, 0xFFC0);
    return out[0] != '\0';
}

bool isAltGame(const uint8_t* rom, size_t romSize)
{
    char title[32];

    read_snes_title_at(title, sizeof(title), rom, romSize, 0x7FC0);
    if (titleLooksAltGame(title))
        return true;

    read_snes_title_at(title, sizeof(title), rom, romSize, 0xFFC0);
    return titleLooksAltGame(title);
}
