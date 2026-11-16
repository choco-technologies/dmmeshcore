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

size_t meshcore_wrap_lines(const char* text, size_t width, char* lines, size_t line_size, size_t max_lines)
{
    size_t count = 0;
    if (width + 1u > line_size)
        width = line_size - 1u;
    const char* rest = text;
    while (count < max_lines)
    {
        while (*rest == ' ')
            rest++;
        if (*rest == '\0')
            break;
        char* line = lines + count * line_size;
        size_t n = fit(rest, width);
        if (rest[n] != '\0' && count + 1u == max_lines)
        {
            /* the last line there is room for: what does not fit ends in "..." */
            n = fit(rest, width > 3u ? width - 3u : 0u);
            memcpy(line, rest, n);
            memcpy(line + n, "\xE2\x80\xA6", 4);
            count++;
            break;
        }
        size_t cut = n;
        if (rest[n] != '\0')
        {
            /* break at the last space of the line when there is one in its second half */
            while (cut > n / 2u && rest[cut] != ' ')
                cut--;
            if (rest[cut] != ' ')
                cut = n;
        }
        size_t end = cut;
        while (end > 0u && rest[end - 1u] == ' ')
            end--;
        memcpy(line, rest, end);
        line[end] = '\0';
        rest += cut;
        count++;
    }
    return count;
}

void meshcore_split_sender(const char* text, char* sender, size_t sender_size, const char** body)
{
    const char* colon = text;
    while (*colon != '\0' && !(colon[0] == ':' && colon[1] == ' '))      /* (no strstr in a module) */
        colon++;
    if (*colon == '\0' || colon == text || (size_t)(colon - text) >= sender_size)
    {
        sender[0] = '\0';
        *body = text;
        return;
    }
    memcpy(sender, text, (size_t)(colon - text));
    sender[colon - text] = '\0';
    *body = colon + 2;
}

void meshcore_bubble_head(const libmeshcore_message_t* message, const char* sender, int offset_min, bool eu_dst, char* out, size_t size)
{
    char clock[8], hops[16];
    meshcore_clock(message->heard_utc, offset_min, eu_dst, clock, sizeof(clock));
    if (message->mine)
        Dmod_SnPrintf(out, size, "You \xC2\xB7 %s", clock);
    else
    {
        if (message->hops == 0u)
            Dmod_SnPrintf(hops, sizeof(hops), "direct");
        else
            Dmod_SnPrintf(hops, sizeof(hops), "%u hop%s", (unsigned)message->hops, message->hops == 1 ? "" : "s");
        Dmod_SnPrintf(out, size, "%s \xC2\xB7 %s \xC2\xB7 %s", sender[0] != '\0' ? sender : "?", clock, hops);
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

static size_t utf8_encode(uint32_t codepoint, char* utf8)
{
    if (codepoint < 0x20u || codepoint == 0x7Fu)
        return 0;
    if (codepoint < 0x80u)
    {
        utf8[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800u)
    {
        utf8[0] = (char)(0xC0u | (codepoint >> 6));
        utf8[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint < 0x10000u)
    {
        utf8[0] = (char)(0xE0u | (codepoint >> 12));
        utf8[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        utf8[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    return 0;
}

size_t meshcore_insert(char* text, size_t capacity, size_t cursor, uint32_t codepoint)
{
    char utf8[4];
    size_t n = utf8_encode(codepoint, utf8);
    size_t length = strlen(text);
    if (cursor > length)
        cursor = length;
    if (n == 0u || length + n + 1u > capacity)
        return cursor;
    memmove(text + cursor + n, text + cursor, length - cursor + 1u);
    memcpy(text + cursor, utf8, n);
    return cursor + n;
}

/* The start of the character that ends at `cursor` */
static size_t previous_start(const char* text, size_t cursor)
{
    if (cursor == 0u)
        return 0u;
    size_t at = cursor - 1u;
    while (at > 0u && ((unsigned char)text[at] & 0xC0u) == 0x80u)
        at--;
    return at;
}

size_t meshcore_delete_before(char* text, size_t cursor)
{
    size_t length = strlen(text);
    if (cursor > length)
        cursor = length;
    size_t start = previous_start(text, cursor);
    if (start == cursor)
        return cursor;
    memmove(text + start, text + cursor, length - cursor + 1u);
    return start;
}

size_t meshcore_cursor_move(const char* text, size_t cursor, int direction)
{
    size_t length = strlen(text);
    if (cursor > length)
        cursor = length;
    if (direction < 0)
        return previous_start(text, cursor);
    if (cursor < length)
        return cursor + char_length(text + cursor);
    return cursor;
}

size_t meshcore_edit_lines(const char* text, size_t width, size_t* starts, size_t max)
{
    size_t count = 0, at = 0;
    do
    {
        if (count >= max)
            break;
        starts[count++] = at;
        size_t n = fit(text + at, width);
        if (text[at + n] != '\0')
        {
            /* a break after the last space of the line, when there is one past its first third */
            size_t cut = n;
            while (cut > n / 3u && text[at + cut - 1u] != ' ')
                cut--;
            n = (cut > n / 3u && text[at + cut - 1u] == ' ') ? cut : n;
        }
        at += n;
    } while (text[at] != '\0');
    return count;
}

size_t meshcore_line_cursor(const char* text, size_t start, size_t end, unsigned zone, unsigned zones)
{
    size_t target = zones > 1u ? start + (end - start) * zone / (zones - 1u) : start;
    size_t at = start;
    while (at < target && at < end)
        at += char_length(text + at);
    if (at > end)
        at = end;
    return at;
}
