#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "format.h"
#include <string.h>

/* The texts of the screen of the MeshCore application */

DMOD_TEST_STEP(age_in_a_unit_that_fits)
{
    char s[16];
    meshcore_age(5, s, sizeof(s));        DMOD_TEST_EXPECT_TRUE(strcmp(s, "5 s") == 0);
    meshcore_age(89, s, sizeof(s));       DMOD_TEST_EXPECT_TRUE(strcmp(s, "89 s") == 0);
    meshcore_age(90, s, sizeof(s));       DMOD_TEST_EXPECT_TRUE(strcmp(s, "2 min") == 0);
    meshcore_age(600, s, sizeof(s));      DMOD_TEST_EXPECT_TRUE(strcmp(s, "10 min") == 0);
    meshcore_age(7200, s, sizeof(s));     DMOD_TEST_EXPECT_TRUE(strcmp(s, "2 h") == 0);
    meshcore_age(200000, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "2 d") == 0);
}

DMOD_TEST_STEP(line_of_a_node)
{
    libmeshcore_contact_t c;
    char s[64];
    memset(&c, 0, sizeof(c));
    c.node_type = LIBMESHCORE_NODE_REPEATER;
    c.hops = 2;
    c.rssi_dbm = -87;
    c.age_s = 300;
    meshcore_node_line(&c, s, sizeof(s));
    DMOD_TEST_EXPECT_TRUE(strcmp(s, "repeater, 2 hops, -87 dBm, 5 min") == 0);
    c.node_type = LIBMESHCORE_NODE_CHAT;
    c.hops = 1;
    meshcore_node_line(&c, s, sizeof(s));
    DMOD_TEST_EXPECT_TRUE(strcmp(s, "companion, 1 hop, -87 dBm, 5 min") == 0);
}

DMOD_TEST_STEP(text_in_two_lines)
{
    char l1[40], l2[40];
    meshcore_wrap("short", 30, l1, l2, sizeof(l1));
    DMOD_TEST_EXPECT_TRUE(strcmp(l1, "short") == 0 && l2[0] == '\0');
    /* a space near the end of the line is where it breaks */
    meshcore_wrap("Tom: it is a long text of a message that goes on", 20, l1, l2, sizeof(l1));
    DMOD_TEST_EXPECT_TRUE(strcmp(l1, "Tom: it is a long") == 0);
    DMOD_TEST_EXPECT_TRUE(strncmp(l2, "text of a message", 17) == 0);
    /* the second line that does not fit ends in an ellipsis (3 bytes of it are kept free) */
    meshcore_wrap("0123456789012345678901234567890123456789012345678901234567890", 20, l1, l2, sizeof(l1));
    DMOD_TEST_EXPECT_EQ((int)strlen(l1), 20);
    DMOD_TEST_EXPECT_TRUE(strcmp(l2 + strlen(l2) - 3, "\xE2\x80\xA6") == 0);
    DMOD_TEST_EXPECT_TRUE(strlen(l2) <= 20);
    /* a multi-byte character is not cut: 7 letters of 2 bytes in 10 bytes are 5 letters */
    meshcore_wrap("\xC4\x85\xC4\x85\xC4\x85\xC4\x85\xC4\x85\xC4\x85\xC4\x85", 10, l1, l2, sizeof(l1));
    DMOD_TEST_EXPECT_EQ((int)strlen(l1), 10);
    DMOD_TEST_EXPECT_EQ((int)strlen(l2), 4);
}

DMOD_TEST_STEP(range_of_a_page)
{
    char s[32];
    meshcore_range(0, 5, 9, s, sizeof(s));  DMOD_TEST_EXPECT_TRUE(strcmp(s, "1-5 of 9") == 0);
    meshcore_range(5, 4, 9, s, sizeof(s));  DMOD_TEST_EXPECT_TRUE(strcmp(s, "6-9 of 9") == 0);
    meshcore_range(0, 0, 0, s, sizeof(s));  DMOD_TEST_EXPECT_TRUE(strcmp(s, "none") == 0);
}

DMOD_TEST_STEP(time_of_day_in_the_local_time)
{
    char s[8];
    /* 2026-11-15T13:05:00Z: CET is +1 h; the summer time is not in November */
    meshcore_clock(1794747900u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "14:05") == 0);
    meshcore_clock(1794747900u, 0, false, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "13:05") == 0);
    /* 2026-07-01T12:00:00Z: +1 h and the summer time */
    meshcore_clock(1782907200u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "14:00") == 0);
    /* the switch: 2026-03-29T00:59:59Z is still winter, 01:00:00Z is summer; 2026-10-25T00:59:59Z is summer, 01:00:00Z winter */
    meshcore_clock(1774745999u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "01:59") == 0);
    meshcore_clock(1774746000u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "03:00") == 0);
    meshcore_clock(1792889999u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "02:59") == 0);
    meshcore_clock(1792890000u, 60, true, s, sizeof(s));   DMOD_TEST_EXPECT_TRUE(strcmp(s, "02:00") == 0);
    meshcore_clock(0, 60, true, s, sizeof(s));             DMOD_TEST_EXPECT_TRUE(strcmp(s, "--:--") == 0);
}

DMOD_TEST_STEP(lines_of_a_bubble)
{
    char lines[6][48];
    /* a short text is a line; the words are not cut; the lines are the width or less */
    DMOD_TEST_EXPECT_EQ((int)meshcore_wrap_lines("short", 25, &lines[0][0], 48, 6), 1);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[0], "short") == 0);
    DMOD_TEST_EXPECT_EQ((int)meshcore_wrap_lines("", 25, &lines[0][0], 48, 6), 0);
    size_t n = meshcore_wrap_lines("czesc wszystkim, jak sie macie dzisiaj wieczorem na gorze", 25, &lines[0][0], 48, 6);
    DMOD_TEST_EXPECT_EQ((int)n, 3);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[0], "czesc wszystkim, jak sie") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[1], "macie dzisiaj wieczorem") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[2], "na gorze") == 0);
    /* what does not fit in the lines there is room for ends in an ellipsis */
    n = meshcore_wrap_lines("aaaa bbbb cccc dddd eeee ffff gggg hhhh", 10, &lines[0][0], 48, 2);
    DMOD_TEST_EXPECT_EQ((int)n, 2);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[0], "aaaa bbbb") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(lines[1] + strlen(lines[1]) - 3, "\xE2\x80\xA6") == 0);
    /* a word longer than the line is cut */
    n = meshcore_wrap_lines("0123456789012345678901234", 10, &lines[0][0], 48, 6);
    DMOD_TEST_EXPECT_EQ((int)n, 3);
    DMOD_TEST_EXPECT_EQ((int)strlen(lines[0]), 10);
}

DMOD_TEST_STEP(head_of_a_bubble)
{
    libmeshcore_message_t m;
    char sender[32], head[64];
    const char* body;
    memset(&m, 0, sizeof(m));
    meshcore_split_sender("Amadis Banino: czesc: wszystkim", sender, sizeof(sender), &body);
    DMOD_TEST_EXPECT_TRUE(strcmp(sender, "Amadis Banino") == 0 && strcmp(body, "czesc: wszystkim") == 0);
    meshcore_split_sender("no sender", sender, sizeof(sender), &body);
    DMOD_TEST_EXPECT_TRUE(sender[0] == '\0' && strcmp(body, "no sender") == 0);
    m.heard_utc = 1794747900u;
    m.hops = 0;
    meshcore_bubble_head(&m, "Amadis", 60, true, head, sizeof(head));   DMOD_TEST_EXPECT_TRUE(strcmp(head, "Amadis \xC2\xB7 14:05 \xC2\xB7 direct") == 0);
    m.hops = 1;
    meshcore_bubble_head(&m, "Amadis", 60, true, head, sizeof(head));   DMOD_TEST_EXPECT_TRUE(strcmp(head, "Amadis \xC2\xB7 14:05 \xC2\xB7 1 hop") == 0);
    m.hops = 3;
    meshcore_bubble_head(&m, "", 60, true, head, sizeof(head));         DMOD_TEST_EXPECT_TRUE(strcmp(head, "? \xC2\xB7 14:05 \xC2\xB7 3 hops") == 0);
    m.mine = true;
    m.heard_utc = 0;                                /* the clock was not set */
    meshcore_bubble_head(&m, "me", 60, true, head, sizeof(head));       DMOD_TEST_EXPECT_TRUE(strcmp(head, "You \xC2\xB7 --:--") == 0);
}

DMOD_TEST_STEP(reply_and_typing)
{
    char s[40];
    meshcore_reply_prefix("Amadis Banino: czesc wszystkim", s, sizeof(s));
    DMOD_TEST_EXPECT_TRUE(strcmp(s, "@[Amadis Banino] ") == 0);
    meshcore_reply_prefix("no sender here", s, sizeof(s));
    DMOD_TEST_EXPECT_TRUE(s[0] == '\0');
    char t[8] = "";
    size_t c = meshcore_insert(t, sizeof(t), 0, 'a');
    c = meshcore_insert(t, sizeof(t), c, 0x105);                          /* a with an ogonek: 2 bytes */
    DMOD_TEST_EXPECT_TRUE(strcmp(t, "a\xC4\x85") == 0 && c == 3u);
    DMOD_TEST_EXPECT_EQ((int)meshcore_insert(t, sizeof(t), c, 0x0A), 3);   /* a control character is not text */
    c = meshcore_delete_before(t, c);                                     /* the whole character goes */
    DMOD_TEST_EXPECT_TRUE(strcmp(t, "a") == 0 && c == 1u);
    c = meshcore_delete_before(t, c);
    DMOD_TEST_EXPECT_EQ((int)meshcore_delete_before(t, c), 0);            /* nothing to remove */
    DMOD_TEST_EXPECT_TRUE(t[0] == '\0');
    char full[3] = "";
    c = meshcore_insert(full, sizeof(full), 0, 'x');
    c = meshcore_insert(full, sizeof(full), c, 'y');
    DMOD_TEST_EXPECT_EQ((int)meshcore_insert(full, sizeof(full), c, 'z'), 2);   /* no room for it and the end: the cursor stays */
    DMOD_TEST_EXPECT_TRUE(strcmp(full, "xy") == 0);
}

DMOD_TEST_STEP(the_cursor_moves_in_the_text)
{
    char t[24] = "abcd";
    /* a character in the middle of a text */
    size_t c = meshcore_insert(t, sizeof(t), 2, 'X');
    DMOD_TEST_EXPECT_TRUE(strcmp(t, "abXcd") == 0 && c == 3u);
    c = meshcore_delete_before(t, c);
    c = meshcore_delete_before(t, c);
    DMOD_TEST_EXPECT_TRUE(strcmp(t, "acd") == 0 && c == 1u);
    /* the arrows move by a whole character */
    strcpy(t, "a\xC4\x85z");
    DMOD_TEST_EXPECT_EQ((int)meshcore_cursor_move(t, 0, 1), 1);
    DMOD_TEST_EXPECT_EQ((int)meshcore_cursor_move(t, 1, 1), 3);
    DMOD_TEST_EXPECT_EQ((int)meshcore_cursor_move(t, 3, -1), 1);
    DMOD_TEST_EXPECT_EQ((int)meshcore_cursor_move(t, 0, -1), 0);
    DMOD_TEST_EXPECT_EQ((int)meshcore_cursor_move(t, 4, 1), 4);
    /* the lines of an edited text lose nothing; a tap puts the cursor in the line */
    size_t starts[8];
    const char* text = "aaaa bbbb cccc dddd";
    size_t n = meshcore_edit_lines(text, 10, starts, 8);
    DMOD_TEST_EXPECT_EQ((int)n, 2);
    DMOD_TEST_EXPECT_EQ((int)starts[0], 0);
    DMOD_TEST_EXPECT_EQ((int)starts[1], 10);                              /* "aaaa bbbb " | "cccc dddd" */
    DMOD_TEST_EXPECT_EQ((int)meshcore_edit_lines("", 10, starts, 8), 1);
    DMOD_TEST_EXPECT_EQ((int)meshcore_edit_lines("0123456789012", 10, starts, 8), 2);
    DMOD_TEST_EXPECT_EQ((int)meshcore_line_cursor(text, 10, 19, 0, 4), 10);
    DMOD_TEST_EXPECT_EQ((int)meshcore_line_cursor(text, 10, 19, 3, 4), 19);
    DMOD_TEST_EXPECT_EQ((int)meshcore_line_cursor(text, 10, 19, 1, 4), 13);
}
