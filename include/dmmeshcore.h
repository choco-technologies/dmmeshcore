#ifndef DMMESHCORE_H
#define DMMESHCORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmmeshcore_defs.h"

/**
 * @file dmmeshcore.h
 * @brief dmmeshcore: the MeshCore mesh protocol (https://github.com/meshcore-dev/MeshCore) - the format of its packets, the adverts, the group
 *        channels. It does not touch the radio: a client reads frames from a LoRa node (DMDRVI_IOCTL_LORA_RECV of dmdrvi, e.g. dmsx126x) and gives them
 *        to this library, and sends what the library builds.
 *
 * A frame on the air is: header (1 byte: route type 2 bits, payload type 4 bits, version 2 bits), the transport codes (2 x 2 bytes, only for the
 * "transport" routes), the length of the path (1 byte: 6 bits the number of hashes, 2 bits the size of a hash less one), the path (the hashes of the
 * nodes the packet went through), the payload (to the end of the frame).
 */

#define DMMESHCORE_MAX_FRAME        255     /**< The longest frame the radio takes. */
#define DMMESHCORE_MAX_PAYLOAD      184     /**< The longest payload of a packet. */
#define DMMESHCORE_MAX_PATH         64      /**< The longest path in bytes (hash_count * hash_size). */
#define DMMESHCORE_PUBLIC_KEY_SIZE  32
#define DMMESHCORE_SIGNATURE_SIZE   64
#define DMMESHCORE_NAME_MAX         32      /**< The longest name in an advert (without the terminator). */
#define DMMESHCORE_CHANNEL_KEY_SIZE 16
#define DMMESHCORE_MAC_SIZE         2       /**< The bytes of the HMAC that go with an encrypted payload. */

/** How a packet is routed: the low 2 bits of the header. */
typedef enum
{
    DMMESHCORE_ROUTE_TRANSPORT_FLOOD  = 0,  /**< Flooded, with the transport codes. */
    DMMESHCORE_ROUTE_FLOOD            = 1,  /**< Flooded: every repeater sends it on, adding its hash to the path. */
    DMMESHCORE_ROUTE_DIRECT           = 2,  /**< Along the path given. */
    DMMESHCORE_ROUTE_TRANSPORT_DIRECT = 3,  /**< Along the path given, with the transport codes. */
} dmmeshcore_route_t;

/** What the payload is: bits 2..5 of the header. */
typedef enum
{
    DMMESHCORE_TYPE_REQ        = 0,         /**< A request to a node (encrypted, with the shared secret). */
    DMMESHCORE_TYPE_RESPONSE   = 1,
    DMMESHCORE_TYPE_TXT_MSG    = 2,         /**< A text message to a node. */
    DMMESHCORE_TYPE_ACK        = 3,
    DMMESHCORE_TYPE_ADVERT     = 4,         /**< A node says who it is (public key, name, place), signed. */
    DMMESHCORE_TYPE_GRP_TXT    = 5,         /**< A text in a group channel. */
    DMMESHCORE_TYPE_GRP_DATA   = 6,
    DMMESHCORE_TYPE_ANON_REQ   = 7,
    DMMESHCORE_TYPE_PATH       = 8,
    DMMESHCORE_TYPE_TRACE      = 9,
    DMMESHCORE_TYPE_MULTIPART  = 10,
    DMMESHCORE_TYPE_CONTROL    = 11,
    DMMESHCORE_TYPE_RAW_CUSTOM = 15,
} dmmeshcore_type_t;

/** The kind of a node, as the advert says it. */
typedef enum
{
    DMMESHCORE_NODE_NONE     = 0,
    DMMESHCORE_NODE_CHAT     = 1,           /**< A companion: a person's device. */
    DMMESHCORE_NODE_REPEATER = 2,
    DMMESHCORE_NODE_ROOM     = 3,           /**< A room server. */
    DMMESHCORE_NODE_SENSOR   = 4,
} dmmeshcore_node_type_t;

/** A packet: the frame taken apart. */
typedef struct
{
    uint8_t     route;                      /**< dmmeshcore_route_t. */
    uint8_t     type;                       /**< dmmeshcore_type_t. */
    uint8_t     version;                    /**< 0..3 (the format of the payload). */
    uint8_t     hash_size;                  /**< The bytes of a hash in the path: 1..3. */
    uint8_t     hash_count;                 /**< The hashes in the path (the hops). */
    uint8_t     payload_length;
    uint16_t    transport_codes[2];         /**< For the transport routes, else 0. */
    uint8_t     path[DMMESHCORE_MAX_PATH];  /**< hash_count * hash_size bytes. */
    uint8_t     payload[DMMESHCORE_MAX_PAYLOAD];
} dmmeshcore_packet_t;

/** An advert (the payload of DMMESHCORE_TYPE_ADVERT). The signature is not checked by dmmeshcore_advert_decode. */
typedef struct
{
    uint8_t     public_key[DMMESHCORE_PUBLIC_KEY_SIZE];
    uint32_t    timestamp;                  /**< Seconds since 1970 by the clock of the sender. */
    uint8_t     signature[DMMESHCORE_SIGNATURE_SIZE];
    uint8_t     node_type;                  /**< dmmeshcore_node_type_t. */
    bool        has_location;
    int32_t     latitude_e6;                /**< Degrees * 1e6. */
    int32_t     longitude_e6;
    char        name[DMMESHCORE_NAME_MAX + 1];
} dmmeshcore_advert_t;

/** A group channel: its secret (16 bytes) and the hash that tells the packets of it (the first byte of SHA-256 of the secret). */
typedef struct
{
    uint8_t     secret[DMMESHCORE_CHANNEL_KEY_SIZE];
    uint8_t     hash;
} dmmeshcore_channel_t;

/** A text of a group channel (the payload of DMMESHCORE_TYPE_GRP_TXT, decrypted). */
typedef struct
{
    uint32_t    timestamp;
    uint8_t     text_type;                  /**< 0: a plain text. */
    uint8_t     attempt;                    /**< 0..3. */
    char        text[DMMESHCORE_MAX_PAYLOAD];   /**< "sender: message", terminated. */
} dmmeshcore_group_text_t;

/**
 * @brief Take a frame apart. 0, -EINVAL (too short, a path that does not fit, a reserved size of a hash), -EMSGSIZE (the payload is longer than
 *        DMMESHCORE_MAX_PAYLOAD).
 */
dmod_dmmeshcore_api(1.0, int, _packet_decode, ( const uint8_t* frame, size_t size, dmmeshcore_packet_t* packet ));

/** @brief Build a frame. 0, -EINVAL (a field out of its range), -ENOSPC (the frame does not fit in `capacity`); `*size` gets its length. */
dmod_dmmeshcore_api(1.0, int, _packet_encode, ( const dmmeshcore_packet_t* packet, uint8_t* frame, size_t capacity, size_t* size ));

/** @brief The name of a route / payload type / node type ("flood", "advert", "repeater"), "?" for what is not known. */
dmod_dmmeshcore_api(1.0, const char*, _route_name, ( uint8_t route ));
dmod_dmmeshcore_api(1.0, const char*, _type_name, ( uint8_t type ));
dmod_dmmeshcore_api(1.0, const char*, _node_type_name, ( uint8_t node_type ));

/** @brief The advert of a packet of type DMMESHCORE_TYPE_ADVERT. 0, -EINVAL (not an advert, or a payload that is too short or has a name that does not fit). */
dmod_dmmeshcore_api(1.0, int, _advert_decode, ( const dmmeshcore_packet_t* packet, dmmeshcore_advert_t* advert ));

/** @brief A channel from its 16-byte secret. */
dmod_dmmeshcore_api(1.0, int, _channel_init, ( dmmeshcore_channel_t* channel, const uint8_t* secret ));

/** @brief A channel from the secret in base64 (as the apps show it: "izOH6cXN6mrJ5e26oRXNcg==" is the public channel) or in 32 hex digits. 0 or -EINVAL. */
dmod_dmmeshcore_api(1.0, int, _channel_from_text, ( dmmeshcore_channel_t* channel, const char* text ));

/**
 * @brief Decrypt a text of a group channel. 0; -ENOENT when the packet is of another channel (the hash does not fit: the payload is not for this
 *        channel); -EBADMSG when the hash fits and the MAC does not (the channel's secret is not the one of the sender); -EINVAL (not a group text,
 *        a length that is not the MAC and a multiple of 16 bytes), -EIO (the crypto is not there).
 */
dmod_dmmeshcore_api(1.0, int, _group_text_decode, ( const dmmeshcore_channel_t* channel, const dmmeshcore_packet_t* packet, dmmeshcore_group_text_t* text ));

/** @brief Build the payload of a group text: encrypted, with the channel hash and the MAC; `packet->type` becomes DMMESHCORE_TYPE_GRP_TXT. 0, -EINVAL, -EIO. */
dmod_dmmeshcore_api(1.0, int, _group_text_encode, ( const dmmeshcore_channel_t* channel, const dmmeshcore_group_text_t* text, dmmeshcore_packet_t* packet ));

#endif // DMMESHCORE_H
