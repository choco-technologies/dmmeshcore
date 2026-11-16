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
