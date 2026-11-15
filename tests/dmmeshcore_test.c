#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmmeshcore.h"
#include <errno.h>
#include <string.h>

/* The packets of MeshCore: what a frame is made of, the adverts, the group channels (with the crypto of dmcrypto) */

static int unhex(const char* hex, uint8_t* out, size_t capacity)
{
    size_t n = strlen(hex) / 2;
    if (n > capacity)
        return -1;
    for (size_t i = 0; i < n; i++)
    {
        int v[2];
        for (int k = 0; k < 2; k++)
        {
            char c = hex[2 * i + k];
            v[k] = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        }
        out[i] = (uint8_t)(v[0] * 16 + v[1]);
    }
    return (int)n;
}

DMOD_TEST_STEP(frame_of_a_mesh_taken_apart)
{
    /* A frame heard on 869.618 MHz (a T-Deck Pro, -51 dBm): a flood, a group text, 5 hashes of 2 bytes in the path; the end of the payload is padded here */
    uint8_t frame[63];
    memset(frame, 0, sizeof(frame));
    int n = unhex("15455208888f4ed8e15f5ad82136dbcb7f29acd674c7259eae3b2dc9b970e8e80ced7f5d0327eb79c6184099dc3379fb", frame, sizeof(frame));
    DMOD_TEST_EXPECT_EQ(n, 48);
    dmmeshcore_packet_t packet;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, sizeof(frame), &packet), 0);
    DMOD_TEST_EXPECT_EQ(packet.route, DMMESHCORE_ROUTE_FLOOD);
    DMOD_TEST_EXPECT_EQ(packet.type, DMMESHCORE_TYPE_GRP_TXT);
    DMOD_TEST_EXPECT_EQ(packet.version, 0);
    DMOD_TEST_EXPECT_EQ(packet.hash_size, 2);
    DMOD_TEST_EXPECT_EQ(packet.hash_count, 5);
    DMOD_TEST_EXPECT_EQ(packet.path[0], 0x52);
    DMOD_TEST_EXPECT_EQ(packet.path[9], 0xD8);
    DMOD_TEST_EXPECT_EQ(packet.payload_length, 51);         /* 1 (channel) + 2 (MAC) + 3 blocks of AES */
    DMOD_TEST_EXPECT_EQ(packet.payload[0], 0x21);
    DMOD_TEST_EXPECT_TRUE(strcmp(dmmeshcore_type_name(packet.type), "group-text") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(dmmeshcore_route_name(packet.route), "flood") == 0);
}

DMOD_TEST_STEP(a_packet_is_built_and_taken_apart_again)
{
    dmmeshcore_packet_t in, out;
    memset(&in, 0, sizeof(in));
    in.route = DMMESHCORE_ROUTE_TRANSPORT_DIRECT;
    in.type = DMMESHCORE_TYPE_TXT_MSG;
    in.version = 1;
    in.transport_codes[0] = 0x1234;
    in.transport_codes[1] = 0xBEEF;
    in.hash_size = 3;
    in.hash_count = 2;
    memcpy(in.path, "abcdef", 6);
    in.payload_length = 4;
    memcpy(in.payload, "\x01\x02\x03\x04", 4);
    uint8_t frame[DMMESHCORE_MAX_FRAME];
    size_t size = 0;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_encode(&in, frame, sizeof(frame), &size), 0);
    DMOD_TEST_EXPECT_EQ((int)size, 1 + 4 + 1 + 6 + 4);
    DMOD_TEST_EXPECT_EQ(frame[0], 0x40 | (2 << 2) | 3);
    DMOD_TEST_EXPECT_EQ(frame[5], 2 | (2 << 6));
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, size, &out), 0);
    DMOD_TEST_EXPECT_TRUE(memcmp(&in, &out, sizeof(in)) == 0);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_encode(&in, frame, 10, &size), -ENOSPC);
}

DMOD_TEST_STEP(bad_frames_are_refused)
{
    dmmeshcore_packet_t p;
    uint8_t frame[DMMESHCORE_MAX_FRAME];
    memset(frame, 0, sizeof(frame));
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 1, &p), -EINVAL);                     /* no path length */
    frame[0] = 0x00;                                                                          /* transport flood: the codes are missing */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 5, &p), -EINVAL);
    frame[0] = 0x01; frame[1] = 5;                                                            /* 5 hashes of 1 byte: no room for them */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 4, &p), -EINVAL);
    frame[1] = 0xC0;                                                                          /* the size of a hash 4: reserved */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 4, &p), -EINVAL);
    frame[1] = 0x00;                                                                          /* a payload of 253 bytes */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 255, &p), -EMSGSIZE);
    frame[1] = 63;                                                                            /* 63 hashes of 1 byte: 63 bytes of the path, fits */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 70, &p), 0);
    frame[1] = 63 | (1 << 6);                                                                 /* 63 hashes of 2: 126 bytes: too long */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, 200, &p), -EINVAL);
}

DMOD_TEST_STEP(advert_of_a_node)
{
    dmmeshcore_packet_t p;
    memset(&p, 0, sizeof(p));
    p.route = DMMESHCORE_ROUTE_FLOOD;
    p.type = DMMESHCORE_TYPE_ADVERT;
    p.hash_size = 1;
    uint8_t* d = p.payload;
    for (int i = 0; i < 32; i++) d[i] = (uint8_t)i;                                          /* the public key */
    d[32] = 0x78; d[33] = 0x56; d[34] = 0x34; d[35] = 0x12;                                  /* 0x12345678 */
    for (int i = 0; i < 64; i++) d[36 + i] = (uint8_t)(0xA0 + i);                            /* the signature */
    size_t at = 100;
    d[at++] = 0x80 | 0x10 | DMMESHCORE_NODE_REPEATER;                                        /* a name, a place, a repeater */
    int32_t lat = 50123456, lon = -19654321;
    memcpy(d + at, &lat, 4); at += 4;
    memcpy(d + at, &lon, 4); at += 4;
    memcpy(d + at, "Hill", 4); at += 4;
    p.payload_length = (uint8_t)at;
    dmmeshcore_advert_t a;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_advert_decode(&p, &a), 0);
    DMOD_TEST_EXPECT_EQ(a.public_key[31], 31);
    DMOD_TEST_EXPECT_EQ((int)a.timestamp, 0x12345678);
    DMOD_TEST_EXPECT_EQ(a.signature[63], 0xA0 + 63);
    DMOD_TEST_EXPECT_EQ(a.node_type, DMMESHCORE_NODE_REPEATER);
    DMOD_TEST_EXPECT_TRUE(a.has_location);
    DMOD_TEST_EXPECT_EQ((int)a.latitude_e6, 50123456);
    DMOD_TEST_EXPECT_EQ((int)a.longitude_e6, -19654321);
    DMOD_TEST_EXPECT_TRUE(strcmp(a.name, "Hill") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(dmmeshcore_node_type_name(a.node_type), "repeater") == 0);
    p.payload_length = 99;                                                                   /* shorter than the key, the time and the signature */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_advert_decode(&p, &a), -EINVAL);
    p.type = DMMESHCORE_TYPE_ACK;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_advert_decode(&p, &a), -EINVAL);
}

DMOD_TEST_STEP(channel_from_its_secret)
{
    dmmeshcore_channel_t c;
    /* the public channel of MeshCore: its hash is 0x11 */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&c, "izOH6cXN6mrJ5e26oRXNcg=="), 0);
    DMOD_TEST_EXPECT_EQ(c.secret[0], 0x8B);
    DMOD_TEST_EXPECT_EQ(c.secret[15], 0x72);
    DMOD_TEST_EXPECT_EQ(c.hash, 0x11);
    dmmeshcore_channel_t same;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&same, "8b3387e9c5cdea6ac9e5edbaa115cd72"), 0);
    DMOD_TEST_EXPECT_TRUE(memcmp(&c, &same, sizeof(c)) == 0);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&same, "short"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&same, "8b3387e9c5cdea6ac9e5edbaa115cd7z"), -EINVAL);
}

DMOD_TEST_STEP(group_text_goes_through_the_channel)
{
    dmmeshcore_channel_t channel, other;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&channel, "izOH6cXN6mrJ5e26oRXNcg=="), 0);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_channel_from_text(&other, "00112233445566778899aabbccddeeff"), 0);
    dmmeshcore_group_text_t in, out;
    memset(&in, 0, sizeof(in));
    in.timestamp = 1763200000u;
    strcpy(in.text, "T-Deck: hello mesh");
    dmmeshcore_packet_t p;
    memset(&p, 0, sizeof(p));
    p.route = DMMESHCORE_ROUTE_FLOOD;
    p.hash_size = 1;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_encode(&channel, &in, &p), 0);
    DMOD_TEST_EXPECT_EQ(p.type, DMMESHCORE_TYPE_GRP_TXT);
    DMOD_TEST_EXPECT_EQ(p.payload[0], 0x11);
    DMOD_TEST_EXPECT_EQ(p.payload_length, 1 + 2 + 32);                                       /* 5 + 18 bytes: two blocks */
    /* through the frame and back, as it goes over the air */
    uint8_t frame[DMMESHCORE_MAX_FRAME];
    size_t size = 0;
    dmmeshcore_packet_t heard;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_encode(&p, frame, sizeof(frame), &size), 0);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_packet_decode(frame, size, &heard), 0);
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_decode(&channel, &heard, &out), 0);
    DMOD_TEST_EXPECT_EQ((int)out.timestamp, (int)in.timestamp);
    DMOD_TEST_EXPECT_EQ(out.text_type, 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(out.text, "T-Deck: hello mesh") == 0);
    /* another channel: not for it; the same hash with another secret: not valid */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_decode(&other, &heard, &out), -ENOENT);
    other.hash = channel.hash;
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_decode(&other, &heard, &out), -EBADMSG);
    heard.payload[10] ^= 1;                                                                  /* a bit of the ciphertext */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_decode(&channel, &heard, &out), -EBADMSG);
    heard.payload_length = 3 + 15;                                                           /* not a multiple of the block */
    DMOD_TEST_EXPECT_EQ(dmmeshcore_group_text_decode(&channel, &heard, &out), -EINVAL);
}
