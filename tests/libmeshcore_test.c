#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "libmeshcore.h"
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
    libmeshcore_packet_t packet;
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, sizeof(frame), &packet), 0);
    DMOD_TEST_EXPECT_EQ(packet.route, LIBMESHCORE_ROUTE_FLOOD);
    DMOD_TEST_EXPECT_EQ(packet.type, LIBMESHCORE_TYPE_GRP_TXT);
    DMOD_TEST_EXPECT_EQ(packet.version, 0);
    DMOD_TEST_EXPECT_EQ(packet.hash_size, 2);
    DMOD_TEST_EXPECT_EQ(packet.hash_count, 5);
    DMOD_TEST_EXPECT_EQ(packet.path[0], 0x52);
    DMOD_TEST_EXPECT_EQ(packet.path[9], 0xD8);
    DMOD_TEST_EXPECT_EQ(packet.payload_length, 51);         /* 1 (channel) + 2 (MAC) + 3 blocks of AES */
    DMOD_TEST_EXPECT_EQ(packet.payload[0], 0x21);
    DMOD_TEST_EXPECT_TRUE(strcmp(libmeshcore_type_name(packet.type), "group-text") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(libmeshcore_route_name(packet.route), "flood") == 0);
}

DMOD_TEST_STEP(a_packet_is_built_and_taken_apart_again)
{
    libmeshcore_packet_t in, out;
    memset(&in, 0, sizeof(in));
    in.route = LIBMESHCORE_ROUTE_TRANSPORT_DIRECT;
    in.type = LIBMESHCORE_TYPE_TXT_MSG;
    in.version = 1;
    in.transport_codes[0] = 0x1234;
    in.transport_codes[1] = 0xBEEF;
    in.hash_size = 3;
    in.hash_count = 2;
    memcpy(in.path, "abcdef", 6);
    in.payload_length = 4;
    memcpy(in.payload, "\x01\x02\x03\x04", 4);
    uint8_t frame[LIBMESHCORE_MAX_FRAME];
    size_t size = 0;
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_encode(&in, frame, sizeof(frame), &size), 0);
    DMOD_TEST_EXPECT_EQ((int)size, 1 + 4 + 1 + 6 + 4);
    DMOD_TEST_EXPECT_EQ(frame[0], 0x40 | (2 << 2) | 3);
    DMOD_TEST_EXPECT_EQ(frame[5], 2 | (2 << 6));
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, size, &out), 0);
    DMOD_TEST_EXPECT_TRUE(memcmp(&in, &out, sizeof(in)) == 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_encode(&in, frame, 10, &size), -ENOSPC);
}

DMOD_TEST_STEP(bad_frames_are_refused)
{
    libmeshcore_packet_t p;
    uint8_t frame[LIBMESHCORE_MAX_FRAME];
    memset(frame, 0, sizeof(frame));
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 1, &p), -EINVAL);                     /* no path length */
    frame[0] = 0x00;                                                                          /* transport flood: the codes are missing */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 5, &p), -EINVAL);
    frame[0] = 0x01; frame[1] = 5;                                                            /* 5 hashes of 1 byte: no room for them */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 4, &p), -EINVAL);
    frame[1] = 0xC0;                                                                          /* the size of a hash 4: reserved */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 4, &p), -EINVAL);
    frame[1] = 0x00;                                                                          /* a payload of 253 bytes */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 255, &p), -EMSGSIZE);
    frame[1] = 63;                                                                            /* 63 hashes of 1 byte: 63 bytes of the path, fits */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 70, &p), 0);
    frame[1] = 63 | (1 << 6);                                                                 /* 63 hashes of 2: 126 bytes: too long */
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, 200, &p), -EINVAL);
}

DMOD_TEST_STEP(advert_of_a_node)
{
    libmeshcore_packet_t p;
    memset(&p, 0, sizeof(p));
    p.route = LIBMESHCORE_ROUTE_FLOOD;
    p.type = LIBMESHCORE_TYPE_ADVERT;
    p.hash_size = 1;
    uint8_t* d = p.payload;
    for (int i = 0; i < 32; i++) d[i] = (uint8_t)i;                                          /* the public key */
    d[32] = 0x78; d[33] = 0x56; d[34] = 0x34; d[35] = 0x12;                                  /* 0x12345678 */
    for (int i = 0; i < 64; i++) d[36 + i] = (uint8_t)(0xA0 + i);                            /* the signature */
    size_t at = 100;
    d[at++] = 0x80 | 0x10 | LIBMESHCORE_NODE_REPEATER;                                        /* a name, a place, a repeater */
    int32_t lat = 50123456, lon = -19654321;
    memcpy(d + at, &lat, 4); at += 4;
    memcpy(d + at, &lon, 4); at += 4;
    memcpy(d + at, "Hill", 4); at += 4;
    p.payload_length = (uint8_t)at;
    libmeshcore_advert_t a;
    DMOD_TEST_EXPECT_EQ(libmeshcore_advert_decode(&p, &a), 0);
    DMOD_TEST_EXPECT_EQ(a.public_key[31], 31);
    DMOD_TEST_EXPECT_EQ((int)a.timestamp, 0x12345678);
    DMOD_TEST_EXPECT_EQ(a.signature[63], 0xA0 + 63);
    DMOD_TEST_EXPECT_EQ(a.node_type, LIBMESHCORE_NODE_REPEATER);
    DMOD_TEST_EXPECT_TRUE(a.has_location);
    DMOD_TEST_EXPECT_EQ((int)a.latitude_e6, 50123456);
    DMOD_TEST_EXPECT_EQ((int)a.longitude_e6, -19654321);
    DMOD_TEST_EXPECT_TRUE(strcmp(a.name, "Hill") == 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(libmeshcore_node_type_name(a.node_type), "repeater") == 0);
    p.payload_length = 99;                                                                   /* shorter than the key, the time and the signature */
    DMOD_TEST_EXPECT_EQ(libmeshcore_advert_decode(&p, &a), -EINVAL);
    p.type = LIBMESHCORE_TYPE_ACK;
    DMOD_TEST_EXPECT_EQ(libmeshcore_advert_decode(&p, &a), -EINVAL);
}

DMOD_TEST_STEP(channel_from_its_secret)
{
    libmeshcore_channel_t c;
    /* the public channel of MeshCore: its hash is 0x11 */
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&c, "izOH6cXN6mrJ5e26oRXNcg=="), 0);
    DMOD_TEST_EXPECT_EQ(c.secret[0], 0x8B);
    DMOD_TEST_EXPECT_EQ(c.secret[15], 0x72);
    DMOD_TEST_EXPECT_EQ(c.hash, 0x11);
    libmeshcore_channel_t same;
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&same, "8b3387e9c5cdea6ac9e5edbaa115cd72"), 0);
    DMOD_TEST_EXPECT_TRUE(memcmp(&c, &same, sizeof(c)) == 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&same, "short"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&same, "8b3387e9c5cdea6ac9e5edbaa115cd7z"), -EINVAL);
}

DMOD_TEST_STEP(group_text_goes_through_the_channel)
{
    libmeshcore_channel_t channel, other;
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&channel, "izOH6cXN6mrJ5e26oRXNcg=="), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&other, "00112233445566778899aabbccddeeff"), 0);
    libmeshcore_group_text_t in, out;
    memset(&in, 0, sizeof(in));
    in.timestamp = 1763200000u;
    strcpy(in.text, "T-Deck: hello mesh");
    libmeshcore_packet_t p;
    memset(&p, 0, sizeof(p));
    p.route = LIBMESHCORE_ROUTE_FLOOD;
    p.hash_size = 1;
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_encode(&channel, &in, &p), 0);
    DMOD_TEST_EXPECT_EQ(p.type, LIBMESHCORE_TYPE_GRP_TXT);
    DMOD_TEST_EXPECT_EQ(p.payload[0], 0x11);
    DMOD_TEST_EXPECT_EQ(p.payload_length, 1 + 2 + 32);                                       /* 5 + 18 bytes: two blocks */
    /* through the frame and back, as it goes over the air */
    uint8_t frame[LIBMESHCORE_MAX_FRAME];
    size_t size = 0;
    libmeshcore_packet_t heard;
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_encode(&p, frame, sizeof(frame), &size), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_packet_decode(frame, size, &heard), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_decode(&channel, &heard, &out), 0);
    DMOD_TEST_EXPECT_EQ((int)out.timestamp, (int)in.timestamp);
    DMOD_TEST_EXPECT_EQ(out.text_type, 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(out.text, "T-Deck: hello mesh") == 0);
    /* another channel: not for it; the same hash with another secret: not valid */
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_decode(&other, &heard, &out), -ENOENT);
    other.hash = channel.hash;
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_decode(&other, &heard, &out), -EBADMSG);
    heard.payload[10] ^= 1;                                                                  /* a bit of the ciphertext */
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_decode(&channel, &heard, &out), -EBADMSG);
    heard.payload_length = 3 + 15;                                                           /* not a multiple of the block */
    DMOD_TEST_EXPECT_EQ(libmeshcore_group_text_decode(&channel, &heard, &out), -EINVAL);
}

DMOD_TEST_STEP(presets_and_settings_are_checked)
{
    libmeshcore_preset_info_t p;
    DMOD_TEST_EXPECT_EQ(libmeshcore_preset(LIBMESHCORE_PRESET_EU_NARROW, &p), 0);
    DMOD_TEST_EXPECT_EQ((int)p.frequency_hz, 869618000);
    DMOD_TEST_EXPECT_EQ((int)p.bandwidth_hz, 62500);
    DMOD_TEST_EXPECT_EQ(p.spreading_factor, 8);
    DMOD_TEST_EXPECT_EQ(p.coding_rate, 8);
    DMOD_TEST_EXPECT_EQ(libmeshcore_preset(LIBMESHCORE_PRESET_COUNT, &p), -EINVAL);
    DMOD_TEST_EXPECT_TRUE(strcmp((libmeshcore_preset(LIBMESHCORE_PRESET_CUSTOM, &p), p.name), "Custom") == 0);
    /* without a radio the settings are what the file (or the defaults) say; a change that does not hold is refused before anything is kept */
    libmeshcore_settings_t s;
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_get(&s), 0);
    s.preset = LIBMESHCORE_PRESET_CUSTOM;
    s.spreading_factor = 13;
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_set(&s), -EINVAL);
    s.spreading_factor = 9;
    s.tx_power_dbm = 30;
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_set(&s), -EINVAL);
    s.tx_power_dbm = 10;
    DMOD_TEST_EXPECT_EQ(s.path_hash_size, 1);                       /* the default */
    s.path_hash_size = 4;
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_set(&s), -EINVAL);
    s.path_hash_size = 0;
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_set(&s), -EINVAL);
    s.path_hash_size = 2;
    s.name[0] = '\0';
    DMOD_TEST_EXPECT_EQ(libmeshcore_settings_set(&s), -EINVAL);
}

DMOD_TEST_STEP(service_without_a_radio)
{
    libmeshcore_status_t st;
    DMOD_TEST_EXPECT_EQ(libmeshcore_status(&st), 0);
    DMOD_TEST_EXPECT_TRUE(!st.running && !st.listening);
    libmeshcore_contact_t c[4];
    libmeshcore_message_t m[4];
    size_t n = 99;
    DMOD_TEST_EXPECT_EQ(libmeshcore_contacts(c, 4, &n), 0);
    DMOD_TEST_EXPECT_EQ((int)n, 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_messages(m, 4, &n), 0);
    DMOD_TEST_EXPECT_EQ((int)n, 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_send_text(0, "hello"), -ENODEV);
    DMOD_TEST_EXPECT_EQ(libmeshcore_send_text(3, "hello"), -ENODEV);
    /* nothing changes: the wait returns the same serial after the time is up */
    DMOD_TEST_EXPECT_EQ((int)libmeshcore_wait(st.serial, 60), (int)st.serial);
}

DMOD_TEST_STEP(a_link_adds_a_channel)
{
    char name[16], secret[48];
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_link_parse("Hi: meshcore://channel/add?name=MikiPatryk&secret=1300235ac2b1d82fa0300f9ce6ec0000 ok", name, sizeof(name), secret, sizeof(secret)), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(name, "MikiPatryk") == 0 && strcmp(secret, "1300235ac2b1d82fa0300f9ce6ec0000") == 0);
    /* the order of the parameters, a space of a name, a parameter that is not known */
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_link_parse("meshcore://channel/add?x=1&secret=izOH6cXN6mrJ5e26oRXNcg==&name=My%20Net+2", name, sizeof(name), secret, sizeof(secret)), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(name, "My Net 2") == 0 && strcmp(secret, "izOH6cXN6mrJ5e26oRXNcg==") == 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_link_parse("no link here", name, sizeof(name), secret, sizeof(secret)), -ENOENT);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_link_parse("meshcore://channel/add?name=OnlyName", name, sizeof(name), secret, sizeof(secret)), -ENOENT);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_link_parse("meshcore://channel/add?name=AVeryLongNameOfAChannel&secret=00", name, sizeof(name), secret, sizeof(secret)), -ENOSPC);
    /* the secret of the public channel is found; one that is not listened to is not */
    uint8_t index = 9;
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_find("izOH6cXN6mrJ5e26oRXNcg==", &index), 0);
    DMOD_TEST_EXPECT_EQ(index, 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_find("ffffffffffffffffffffffffffffffff", &index), -ENOENT);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_find("junk", &index), -EINVAL);
}

DMOD_TEST_STEP(channels_are_added_once)
{
    char name[16];
    uint8_t hash = 0;
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_name(0, name, sizeof(name), &hash), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(name, "Public") == 0);
    DMOD_TEST_EXPECT_EQ(hash, 0x11);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("Friends", "00112233445566778899aabbccddeeff"), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("Again", "00112233445566778899aabbccddeeff"), -EEXIST);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("Public too", "izOH6cXN6mrJ5e26oRXNcg=="), -EEXIST);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("Bad", "not a key"), -EINVAL);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_name(1, name, sizeof(name), &hash), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(name, "Friends") == 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_name(2, name, sizeof(name), &hash), -ENOENT);

    /* a channel of a name needs no key: the key is made of the name (and the places after the removed one are free again) */
    libmeshcore_channel_t a, b;
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&a, "#test"), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&b, "#test"), 0);
    DMOD_TEST_EXPECT_EQ(memcmp(a.secret, b.secret, sizeof(a.secret)), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_from_text(&b, "#other"), 0);
    DMOD_TEST_EXPECT_TRUE(memcmp(a.secret, b.secret, sizeof(a.secret)) != 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("#test", NULL), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("#test", ""), -EEXIST);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_add("plain", NULL), -EINVAL);          /* no '#' and no key */
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_name(2, name, sizeof(name), &hash), 0);
    DMOD_TEST_EXPECT_TRUE(strcmp(name, "#test") == 0);
    DMOD_TEST_EXPECT_EQ(hash, a.hash);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_remove(0), -EINVAL);                    /* the public one stays */
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_remove(2), 0);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_remove(2), -ENOENT);
    DMOD_TEST_EXPECT_EQ(libmeshcore_channel_name(2, name, sizeof(name), &hash), -ENOENT);
}
