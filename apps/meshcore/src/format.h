#ifndef MESHCORE_FORMAT_H
#define MESHCORE_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "libmeshcore.h"

/* The texts of the screen, made of what libmeshcore says (no screen, no radio: tested on a PC) */

/** "12 s", "5 min", "3 h", "2 d". */
void meshcore_age(uint32_t seconds, char* out, size_t size);

/** The second line of a node: "repeater, 2 hops, -87 dBm, 5 min". */
void meshcore_node_line(const libmeshcore_contact_t* contact, char* out, size_t size);

/** A text in two lines of at most `width` bytes (a multi-byte character is not cut; what does not fit ends in "…"). */
void meshcore_wrap(const char* text, size_t width, char* line1, char* line2, size_t size);

/** "1-5 of 9" (or "none") for the page of a list that starts at `first` (0-based). */
void meshcore_range(size_t first, size_t shown, size_t total, char* out, size_t size);

/**
 * @brief The time of day of a moment (seconds since 1970, UTC) in the local time as "08:35": `offset_min` from UTC (60: CET), `eu_dst` adds the summer time of the
 *        European Union (from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC). Without a time (0): "--:--".
 */
void meshcore_clock(uint32_t utc_seconds, int offset_min, bool eu_dst, char* out, size_t size);

/** @brief The first line of a text: "Public, 08:35, 3 hops" (no time when the clock was not set: "Public, 3 hops"; "> " before the channel of a text of this node). */
void meshcore_who(const libmeshcore_message_t* message, int offset_min, bool eu_dst, char* out, size_t size);

/** @brief What a reply to a text begins with: "@[Sender] " (the text is "Sender: message"); empty when there is no sender in it. */
void meshcore_reply_prefix(const char* text, char* out, size_t size);

/** @brief Append a character (a Unicode code point) to a text as UTF-8 within `capacity` bytes (with the end). true when it was appended. */
bool meshcore_append(char* text, size_t capacity, uint32_t codepoint);

/** @brief Remove the last character (a multi-byte one as a whole). */
void meshcore_backspace(char* text);

#endif
