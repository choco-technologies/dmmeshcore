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
