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

/**
 * @brief `text` in lines of at most `width` bytes, broken at spaces (a multi-byte character is not cut): `lines` is `max_lines` places of `line_size`
 *        bytes. The last line ends in "…" when the text does not fit. Returns the lines made (0 for an empty text).
 */
size_t meshcore_wrap_lines(const char* text, size_t width, char* lines, size_t line_size, size_t max_lines);

/** @brief A text of a channel is "Sender: message": the sender (empty when there is none) and where the message begins. */
void meshcore_split_sender(const char* text, char* sender, size_t sender_size, const char** body);

/** @brief The head of a bubble: "Sender · 08:35 · 2 hops" (or "direct"), of the text of this node "You · 08:35". `sender` comes from meshcore_split_sender. */
void meshcore_bubble_head(const libmeshcore_message_t* message, const char* sender, int offset_min, bool eu_dst, char* out, size_t size);

/** "1-5 of 9" (or "none") for the page of a list that starts at `first` (0-based). */
void meshcore_range(size_t first, size_t shown, size_t total, char* out, size_t size);

/**
 * @brief The time of day of a moment (seconds since 1970, UTC) in the local time as "08:35": `offset_min` from UTC (60: CET), `eu_dst` adds the summer time of the
 *        European Union (from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC). Without a time (0): "--:--".
 */
void meshcore_clock(uint32_t utc_seconds, int offset_min, bool eu_dst, char* out, size_t size);

/** @brief What a reply to a text begins with: "@[Sender] " (the text is "Sender: message"); empty when there is no sender in it. */
void meshcore_reply_prefix(const char* text, char* out, size_t size);

/** @brief Insert a character (a Unicode code point) at the byte `cursor` of a text as UTF-8 within `capacity` bytes (with the end). Returns the cursor after it (the same when it did not fit). */
size_t meshcore_insert(char* text, size_t capacity, size_t cursor, uint32_t codepoint);

/** @brief Remove the character before the cursor (a multi-byte one as a whole). Returns the cursor. */
size_t meshcore_delete_before(char* text, size_t cursor);

/** @brief The cursor one character to the left (`direction` < 0) or to the right. */
size_t meshcore_cursor_move(const char* text, size_t cursor, int direction);

/**
 * @brief The lines of a text that is edited: where each begins (`starts`, at most `max`) when the lines are `width` bytes at most, broken after a space where there is one.
 *        Nothing is dropped (unlike meshcore_wrap_lines): line k is text[starts[k]..starts[k+1]). Always at least one line. Returns the lines.
 */
size_t meshcore_edit_lines(const char* text, size_t width, size_t* starts, size_t max);

/** @brief Where a tap puts the cursor: `zone` (0..zones-1) of the width of a line that is text[start..end): the first one is its start, the last one its end. */
size_t meshcore_line_cursor(const char* text, size_t start, size_t end, unsigned zone, unsigned zones);

#endif
