#include "dmod.h"
#include "format.h"
#include <string.h>

void meshcore_age(uint32_t seconds, char* out, size_t size)
{
    if (seconds < 90u)
        Dmod_SnPrintf(out, size, "%u s", (unsigned)seconds);
    else if (seconds < 5400u)
        Dmod_SnPrintf(out, size, "%u min", (unsigned)((seconds + 30u) / 60u));
    else if (seconds < 129600u)
        Dmod_SnPrintf(out, size, "%u h", (unsigned)((seconds + 1800u) / 3600u));
    else
        Dmod_SnPrintf(out, size, "%u d", (unsigned)((seconds + 43200u) / 86400u));
}

void meshcore_node_line(const libmeshcore_contact_t* contact, char* out, size_t size)
{
    char age[16];
    meshcore_age(contact->age_s, age, sizeof(age));
    const char* kind = contact->node_type == LIBMESHCORE_NODE_REPEATER ? "repeater" : contact->node_type == LIBMESHCORE_NODE_ROOM ? "room" :
                       contact->node_type == LIBMESHCORE_NODE_SENSOR ? "sensor" : contact->node_type == LIBMESHCORE_NODE_CHAT ? "companion" : "node";
    Dmod_SnPrintf(out, size, "%s, %u hop%s, %d dBm, %s", kind, (unsigned)contact->hops, contact->hops == 1 ? "" : "s", (int)contact->rssi_dbm, age);
}

/* The bytes of the character that starts at text[0] (1 for what is not valid) */
static size_t char_length(const char* text)
{
    unsigned char c = (unsigned char)text[0];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    for (size_t i = 1; i < n; i++)
        if (((unsigned char)text[i] & 0xC0) != 0x80)
            return 1;
    return n;
}

/* How many bytes of `text` fit in `width` bytes without cutting a character */
static size_t fit(const char* text, size_t width)
{
    size_t at = 0;
    while (text[at] != '\0')
    {
        size_t n = char_length(text + at);
        if (at + n > width)
            break;
        at += n;
    }
    return at;
}

void meshcore_wrap(const char* text, size_t width, char* line1, char* line2, size_t size)
{
    line1[0] = '\0';
    line2[0] = '\0';
    if (width + 1u > size)
        width = size - 1u;
    size_t first = fit(text, width);
    if (text[first] != '\0')
    {
        /* break at the last space of the line when there is one in its second half */
        size_t cut = first;
        while (cut > first / 2u && text[cut] != ' ')
            cut--;
        if (text[cut] == ' ')
            first = cut;
    }
    memcpy(line1, text, first);
    line1[first] = '\0';
    const char* rest = text + first;
    while (*rest == ' ')
        rest++;
    size_t second = fit(rest, width);
    if (rest[second] != '\0')
    {
        /* too long: the line ends in "…" (3 bytes) */
        second = fit(rest, width > 3u ? width - 3u : 0u);
        memcpy(line2, rest, second);
        memcpy(line2 + second, "\xE2\x80\xA6", 4);
    }
    else
    {
        memcpy(line2, rest, second);
        line2[second] = '\0';
    }
}

void meshcore_range(size_t first, size_t shown, size_t total, char* out, size_t size)
{
    if (total == 0u || shown == 0u)
        Dmod_SnPrintf(out, size, "none");
    else
        Dmod_SnPrintf(out, size, "%u-%u of %u", (unsigned)(first + 1u), (unsigned)(first + shown), (unsigned)total);
}

/* ---- The time ---- */

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int64_t y = year - (month <= 2 ? 1 : 0);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int year_of(int64_t days)
{
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    return (int)((int64_t)yoe + era * 400 + (mp >= 10 ? 1 : 0));
}

/* 01:00 UTC of the last Sunday of a month of 31 days */
static int64_t last_sunday_01utc(int year, unsigned month)
{
    int64_t last = days_from_civil(year, month, 31);
    unsigned weekday = (unsigned)((last % 7 + 7 + 4) % 7);          /* 1970-01-01 was a Thursday; 0 = Sunday */
    return (last - weekday) * 86400 + 3600;
}

void meshcore_clock(uint32_t utc_seconds, int offset_min, bool eu_dst, char* out, size_t size)
{
    if (utc_seconds == 0u)
    {
        Dmod_SnPrintf(out, size, "--:--");
        return;
    }
    int64_t utc = utc_seconds;
    int64_t local = utc + (int64_t)offset_min * 60;
    if (eu_dst)
    {
        int year = year_of(utc / 86400);
        if (utc >= last_sunday_01utc(year, 3) && utc < last_sunday_01utc(year, 10))
            local += 3600;
    }
    int64_t rest = local % 86400;
    if (rest < 0)
        rest += 86400;
    Dmod_SnPrintf(out, size, "%02u:%02u", (unsigned)(rest / 3600), (unsigned)(rest % 3600 / 60));
}

void meshcore_who(const libmeshcore_message_t* message, int offset_min, bool eu_dst, char* out, size_t size)
{
    char clock[8];
    const char* mine = message->mine ? "> " : "";
    if (message->heard_utc != 0u)
    {
        meshcore_clock(message->heard_utc, offset_min, eu_dst, clock, sizeof(clock));
        Dmod_SnPrintf(out, size, "%s%s, %s, %u hop%s", mine, message->channel, clock, (unsigned)message->hops, message->hops == 1 ? "" : "s");
    }
    else
        Dmod_SnPrintf(out, size, "%s%s, %u hop%s", mine, message->channel, (unsigned)message->hops, message->hops == 1 ? "" : "s");
}

void meshcore_reply_prefix(const char* text, char* out, size_t size)
{
    out[0] = '\0';
    const char* colon = text;
    while (*colon != '\0' && !(colon[0] == ':' && colon[1] == ' '))      /* (no strstr in a module) */
        colon++;
    if (*colon == '\0' || colon == text || (size_t)(colon - text) + 5u > size)
        return;
    Dmod_SnPrintf(out, size, "@[%.*s] ", (int)(colon - text), text);
}

bool meshcore_append(char* text, size_t capacity, uint32_t codepoint)
{
    char utf8[4];
    size_t n;
    if (codepoint < 0x20u || codepoint == 0x7Fu)
        return false;
    if (codepoint < 0x80u)
    {
        utf8[0] = (char)codepoint;
        n = 1;
    }
    else if (codepoint < 0x800u)
    {
        utf8[0] = (char)(0xC0u | (codepoint >> 6));
        utf8[1] = (char)(0x80u | (codepoint & 0x3Fu));
        n = 2;
    }
    else if (codepoint < 0x10000u)
    {
        utf8[0] = (char)(0xE0u | (codepoint >> 12));
        utf8[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        utf8[2] = (char)(0x80u | (codepoint & 0x3Fu));
        n = 3;
    }
    else
        return false;
    size_t length = strlen(text);
    if (length + n + 1u > capacity)
        return false;
    memcpy(text + length, utf8, n);
    text[length + n] = '\0';
    return true;
}

void meshcore_backspace(char* text)
{
    size_t length = strlen(text);
    while (length > 0u && ((unsigned char)text[length - 1u] & 0xC0u) == 0x80u)
        length--;                                                   /* the continuation bytes of the last character */
    if (length > 0u)
        length--;
    text[length] = '\0';
}
