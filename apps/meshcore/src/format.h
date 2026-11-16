#ifndef MESHCORE_FORMAT_H
#define MESHCORE_FORMAT_H

#include <stddef.h>
#include <stdint.h>
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

#endif
