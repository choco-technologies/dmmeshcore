/* (the registrations of the API are in packet.c: only one file of a module has DMOD_ENABLE_REGISTRATION) */
#include "dmod.h"
#include "libmeshcore.h"
#include "dmcrypto.h"
#include <errno.h>
#include <string.h>

/* The group channels of MeshCore: the secret is 16 bytes; the hash of the channel is the first byte of SHA-256 of it; a payload is
 * [hash][MAC: 2 bytes of HMAC-SHA256 of the ciphertext with the secret][AES-128-ECB of (timestamp u32 LE, flags, text) padded with zeros]. */

#define AES_BLOCK 16u

/* AES-128 of one block (ECB): the CBC of dmcrypto with a zero IV on a single block */
static int aes_block(bool encrypt, const uint8_t* key, const uint8_t* in, uint8_t* out)
{
    uint8_t iv[AES_BLOCK] = { 0 };
    return dmcrypto_cipher_crypt(DMCRYPTO_CIPHER_AES_CBC, encrypt, key, LIBMESHCORE_CHANNEL_KEY_SIZE, iv, in, out, AES_BLOCK) == DMCRYPTO_OK ? 0 : -EIO;
}

static int mac_of(const uint8_t* secret, const uint8_t* data, size_t size, uint8_t mac[LIBMESHCORE_MAC_SIZE])
{
    uint8_t full[32];
    size_t length = 0;
    /* (MeshCore keys the HMAC with the secret padded with zeros to 32 bytes: the same as the 16 bytes, HMAC pads the key itself) */
    if (dmcrypto_hmac(DMCRYPTO_HASH_SHA256, secret, LIBMESHCORE_CHANNEL_KEY_SIZE, data, size, full, sizeof(full), &length) != DMCRYPTO_OK || length < sizeof(full))
        return -EIO;
    memcpy(mac, full, LIBMESHCORE_MAC_SIZE);
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_init, ( libmeshcore_channel_t* channel, const uint8_t* secret ))
{
    if (channel == NULL || secret == NULL)
        return -EINVAL;
    uint8_t digest[32];
    size_t length = 0;
    memcpy(channel->secret, secret, LIBMESHCORE_CHANNEL_KEY_SIZE);
    if (dmcrypto_hash(DMCRYPTO_HASH_SHA256, secret, LIBMESHCORE_CHANNEL_KEY_SIZE, digest, sizeof(digest), &length) != DMCRYPTO_OK || length < 1u)
        return -EIO;
    channel->hash = digest[0];
    return 0;
}

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_from_text, ( libmeshcore_channel_t* channel, const char* text ))
{
    if (channel == NULL || text == NULL)
        return -EINVAL;
    size_t length = strlen(text);
    uint8_t secret[LIBMESHCORE_CHANNEL_KEY_SIZE];
    if (text[0] == '#' && length > 1u)
    {
        /* A channel of a name ("#test"): the key is the first 16 bytes of SHA-256 of the name (with the #), so everybody who knows the name has it */
        uint8_t digest[32];
        size_t size = 0;
        if (dmcrypto_hash(DMCRYPTO_HASH_SHA256, (const uint8_t*)text, length, digest, sizeof(digest), &size) != DMCRYPTO_OK || size < sizeof(secret))
            return -EIO;
        memcpy(secret, digest, sizeof(secret));
        return libmeshcore_channel_init(channel, secret);
    }
    if (length == 2u * LIBMESHCORE_CHANNEL_KEY_SIZE)
    {
        for (size_t i = 0; i < LIBMESHCORE_CHANNEL_KEY_SIZE; i++)
        {
            int hi = hex_value(text[2u * i]), lo = hex_value(text[2u * i + 1u]);
            if (hi < 0 || lo < 0)
                return -EINVAL;
            secret[i] = (uint8_t)(hi * 16 + lo);
        }
        return libmeshcore_channel_init(channel, secret);
    }
    /* base64 of 16 bytes: 22 characters, with "==" 24 */
    uint32_t bits = 0;
    unsigned have = 0;
    size_t out = 0;
    for (size_t i = 0; i < length && text[i] != '='; i++)
    {
        int v = base64_value(text[i]);
        if (v < 0)
            return -EINVAL;
        bits = (bits << 6) | (uint32_t)v;
        have += 6u;
        if (have >= 8u)
        {
            have -= 8u;
            if (out >= LIBMESHCORE_CHANNEL_KEY_SIZE)
                return -EINVAL;
            secret[out++] = (uint8_t)(bits >> have);
        }
    }
    if (out != LIBMESHCORE_CHANNEL_KEY_SIZE)
        return -EINVAL;
    return libmeshcore_channel_init(channel, secret);
}

dmod_libmeshcore_api_declaration(1.0, int, _group_text_decode, ( const libmeshcore_channel_t* channel, const libmeshcore_packet_t* packet, libmeshcore_group_text_t* text ))
{
    if (channel == NULL || packet == NULL || text == NULL || packet->type != LIBMESHCORE_TYPE_GRP_TXT)
        return -EINVAL;
    memset(text, 0, sizeof(*text));
    size_t cipher = packet->payload_length < 1u + LIBMESHCORE_MAC_SIZE ? 0u : (size_t)packet->payload_length - 1u - LIBMESHCORE_MAC_SIZE;
    if (cipher == 0u || cipher % AES_BLOCK != 0u)
        return -EINVAL;
    if (packet->payload[0] != channel->hash)
        return -ENOENT;
    uint8_t mac[LIBMESHCORE_MAC_SIZE];
    const uint8_t* data = packet->payload + 1u + LIBMESHCORE_MAC_SIZE;
    int ret = mac_of(channel->secret, data, cipher, mac);
    if (ret != 0)
        return ret;
    if (memcmp(mac, packet->payload + 1u, LIBMESHCORE_MAC_SIZE) != 0)
        return -EBADMSG;
    uint8_t plain[LIBMESHCORE_MAX_PAYLOAD];
    for (size_t at = 0; at < cipher; at += AES_BLOCK)
    {
        ret = aes_block(false, channel->secret, data + at, plain + at);
        if (ret != 0)
            return ret;
    }
    if (cipher < 5u)
        return -EINVAL;
    text->timestamp = (uint32_t)plain[0] | ((uint32_t)plain[1] << 8) | ((uint32_t)plain[2] << 16) | ((uint32_t)plain[3] << 24);
    text->text_type = (uint8_t)(plain[4] >> 2);
    text->attempt = (uint8_t)(plain[4] & 0x03u);
    size_t length = cipher - 5u;
    memcpy(text->text, plain + 5, length);
    text->text[length < sizeof(text->text) ? length : sizeof(text->text) - 1u] = '\0';   /* (the padding is zeros: the text ends at the first) */
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, int, _group_text_encode, ( const libmeshcore_channel_t* channel, const libmeshcore_group_text_t* text, libmeshcore_packet_t* packet ))
{
    if (channel == NULL || text == NULL || packet == NULL)
        return -EINVAL;
    size_t text_length = strlen(text->text);
    size_t plain_length = 5u + text_length;
    size_t cipher = (plain_length + AES_BLOCK - 1u) / AES_BLOCK * AES_BLOCK;
    if (1u + LIBMESHCORE_MAC_SIZE + cipher > LIBMESHCORE_MAX_PAYLOAD)
        return -EINVAL;
    uint8_t plain[LIBMESHCORE_MAX_PAYLOAD];
    memset(plain, 0, cipher);
    plain[0] = (uint8_t)text->timestamp;
    plain[1] = (uint8_t)(text->timestamp >> 8);
    plain[2] = (uint8_t)(text->timestamp >> 16);
    plain[3] = (uint8_t)(text->timestamp >> 24);
    plain[4] = (uint8_t)((text->text_type << 2) | (text->attempt & 0x03u));
    memcpy(plain + 5, text->text, text_length);
    uint8_t* out = packet->payload + 1u + LIBMESHCORE_MAC_SIZE;
    for (size_t at = 0; at < cipher; at += AES_BLOCK)
    {
        int ret = aes_block(true, channel->secret, plain + at, out + at);
        if (ret != 0)
            return ret;
    }
    uint8_t mac[LIBMESHCORE_MAC_SIZE];
    int ret = mac_of(channel->secret, out, cipher, mac);
    if (ret != 0)
        return ret;
    packet->type = LIBMESHCORE_TYPE_GRP_TXT;
    packet->payload[0] = channel->hash;
    memcpy(packet->payload + 1, mac, LIBMESHCORE_MAC_SIZE);
    packet->payload_length = (uint8_t)(1u + LIBMESHCORE_MAC_SIZE + cipher);
    return 0;
}

/* ---- The link that adds a channel ---- */

#define CHANNEL_LINK    "meshcore://channel/add?"

static int hex_digit(char c)
{
    return hex_value(c);
}

/* A value of the link up to '&' or a space, with "%20" and "+" decoded, into out; false when it does not fit */
static bool link_value(const char* from, char* out, size_t size, const char** end)   /* out may be NULL: the value is skipped */
{
    size_t o = 0;
    const char* c = from;
    while (*c != '\0' && *c != '&' && *c != ' ' && *c != '\n' && *c != '\r' && *c != '\t')
    {
        char ch = *c;
        if (ch == '+')
            ch = ' ';
        else if (ch == '%' && hex_digit(c[1]) >= 0 && hex_digit(c[2]) >= 0)
        {
            ch = (char)(hex_digit(c[1]) * 16 + hex_digit(c[2]));
            c += 2;
        }
        if (out != NULL)
        {
            if (o + 1u >= size)
                return false;
            out[o] = ch;
        }
        o++;
        c++;
    }
    if (out != NULL)
        out[o] = '\0';
    *end = c;
    return true;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_link_parse, ( const char* text, char* name, size_t name_size, char* secret, size_t secret_size ))
{
    if (text == NULL || name == NULL || name_size == 0 || secret == NULL || secret_size == 0)
        return -EINVAL;
    const size_t prefix = sizeof(CHANNEL_LINK) - 1u;
    const char* at = text;
    while (*at != '\0' && strncmp(at, CHANNEL_LINK, prefix) != 0)      /* (no strstr in a module) */
        at++;
    if (*at == '\0')
        return -ENOENT;
    at += prefix;
    name[0] = secret[0] = '\0';
    while (*at != '\0' && *at != ' ')
    {
        const char* equal = at;
        while (*equal != '\0' && *equal != '=' && *equal != '&' && *equal != ' ')
            equal++;
        if (*equal != '=')
            break;
        char* target = NULL;
        size_t target_size = 0;
        if (equal - at == 4 && strncmp(at, "name", 4) == 0)
            target = name, target_size = name_size;
        else if (equal - at == 6 && strncmp(at, "secret", 6) == 0)
            target = secret, target_size = secret_size;
        const char* end = equal + 1;
        if (!link_value(equal + 1, target, target_size, &end))      /* (a parameter that is not known has no target: it is skipped) */
            return -ENOSPC;
        at = (*end == '&') ? end + 1 : end;
    }
    return (name[0] != '\0' && secret[0] != '\0') ? 0 : -ENOENT;
}
