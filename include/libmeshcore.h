#ifndef LIBMESHCORE_H
#define LIBMESHCORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "libmeshcore_defs.h"

/**
 * @file libmeshcore.h
 * @brief libmeshcore: the MeshCore mesh (https://github.com/meshcore-dev/MeshCore) - everything but the screen. A library service: libsystemd starts
 *        it for every LoRa radio that dmdevfs reports (the class lora, configs/libmeshcore.rules), it listens on the radio, takes the packets
 *        apart, keeps the nodes it heard (adverts) and the texts of the channels it has the key of, and keeps the settings of the radio. The
 *        applications (the meshcore GUI, the mesh tool, Settings) are only a view of it: they call the functions below.
 *
 *        The first part is the protocol itself, with no radio and no state: the codec of the packets, the adverts, the group channels.
 *
 * A frame on the air is: header (1 byte: route type 2 bits, payload type 4 bits, version 2 bits), the transport codes (2 x 2 bytes, only for the
 * "transport" routes), the length of the path (1 byte: 6 bits the number of hashes, 2 bits the size of a hash less one), the path (the hashes of the
 * nodes the packet went through), the payload (to the end of the frame).
 */

#define LIBMESHCORE_MAX_FRAME        255     /**< The longest frame the radio takes. */
#define LIBMESHCORE_MAX_PAYLOAD      184     /**< The longest payload of a packet. */
#define LIBMESHCORE_MAX_PATH         64      /**< The longest path in bytes (hash_count * hash_size). */
#define LIBMESHCORE_PUBLIC_KEY_SIZE  32
#define LIBMESHCORE_SIGNATURE_SIZE   64
#define LIBMESHCORE_NAME_MAX         32      /**< The longest name in an advert (without the terminator). */
#define LIBMESHCORE_CHANNEL_KEY_SIZE 16
#define LIBMESHCORE_MAC_SIZE         2       /**< The bytes of the HMAC that go with an encrypted payload. */

/** How a packet is routed: the low 2 bits of the header. */
typedef enum
{
    LIBMESHCORE_ROUTE_TRANSPORT_FLOOD  = 0,  /**< Flooded, with the transport codes. */
    LIBMESHCORE_ROUTE_FLOOD            = 1,  /**< Flooded: every repeater sends it on, adding its hash to the path. */
    LIBMESHCORE_ROUTE_DIRECT           = 2,  /**< Along the path given. */
    LIBMESHCORE_ROUTE_TRANSPORT_DIRECT = 3,  /**< Along the path given, with the transport codes. */
} libmeshcore_route_t;

/** What the payload is: bits 2..5 of the header. */
typedef enum
{
    LIBMESHCORE_TYPE_REQ        = 0,         /**< A request to a node (encrypted, with the shared secret). */
    LIBMESHCORE_TYPE_RESPONSE   = 1,
    LIBMESHCORE_TYPE_TXT_MSG    = 2,         /**< A text message to a node. */
    LIBMESHCORE_TYPE_ACK        = 3,
    LIBMESHCORE_TYPE_ADVERT     = 4,         /**< A node says who it is (public key, name, place), signed. */
    LIBMESHCORE_TYPE_GRP_TXT    = 5,         /**< A text in a group channel. */
    LIBMESHCORE_TYPE_GRP_DATA   = 6,
    LIBMESHCORE_TYPE_ANON_REQ   = 7,
    LIBMESHCORE_TYPE_PATH       = 8,
    LIBMESHCORE_TYPE_TRACE      = 9,
    LIBMESHCORE_TYPE_MULTIPART  = 10,
    LIBMESHCORE_TYPE_CONTROL    = 11,
    LIBMESHCORE_TYPE_RAW_CUSTOM = 15,
} libmeshcore_type_t;

/** The kind of a node, as the advert says it. */
typedef enum
{
    LIBMESHCORE_NODE_NONE     = 0,
    LIBMESHCORE_NODE_CHAT     = 1,           /**< A companion: a person's device. */
    LIBMESHCORE_NODE_REPEATER = 2,
    LIBMESHCORE_NODE_ROOM     = 3,           /**< A room server. */
    LIBMESHCORE_NODE_SENSOR   = 4,
} libmeshcore_node_type_t;

/** A packet: the frame taken apart. */
typedef struct
{
    uint8_t     route;                      /**< libmeshcore_route_t. */
    uint8_t     type;                       /**< libmeshcore_type_t. */
    uint8_t     version;                    /**< 0..3 (the format of the payload). */
    uint8_t     hash_size;                  /**< The bytes of a hash in the path: 1..3. */
    uint8_t     hash_count;                 /**< The hashes in the path (the hops). */
    uint8_t     payload_length;
    uint16_t    transport_codes[2];         /**< For the transport routes, else 0. */
    uint8_t     path[LIBMESHCORE_MAX_PATH];  /**< hash_count * hash_size bytes. */
    uint8_t     payload[LIBMESHCORE_MAX_PAYLOAD];
} libmeshcore_packet_t;

/** An advert (the payload of LIBMESHCORE_TYPE_ADVERT). The signature is not checked by libmeshcore_advert_decode. */
typedef struct
{
    uint8_t     public_key[LIBMESHCORE_PUBLIC_KEY_SIZE];
    uint32_t    timestamp;                  /**< Seconds since 1970 by the clock of the sender. */
    uint8_t     signature[LIBMESHCORE_SIGNATURE_SIZE];
    uint8_t     node_type;                  /**< libmeshcore_node_type_t. */
    bool        has_location;
    int32_t     latitude_e6;                /**< Degrees * 1e6. */
    int32_t     longitude_e6;
    char        name[LIBMESHCORE_NAME_MAX + 1];
} libmeshcore_advert_t;

/** A group channel: its secret (16 bytes) and the hash that tells the packets of it (the first byte of SHA-256 of the secret). */
typedef struct
{
    uint8_t     secret[LIBMESHCORE_CHANNEL_KEY_SIZE];
    uint8_t     hash;
} libmeshcore_channel_t;

/** A text of a group channel (the payload of LIBMESHCORE_TYPE_GRP_TXT, decrypted). */
typedef struct
{
    uint32_t    timestamp;
    uint8_t     text_type;                  /**< 0: a plain text. */
    uint8_t     attempt;                    /**< 0..3. */
    char        text[LIBMESHCORE_MAX_PAYLOAD];   /**< "sender: message", terminated. */
} libmeshcore_group_text_t;

/**
 * @brief Take a frame apart. 0, -EINVAL (too short, a path that does not fit, a reserved size of a hash), -EMSGSIZE (the payload is longer than
 *        LIBMESHCORE_MAX_PAYLOAD).
 */
dmod_libmeshcore_api(1.0, int, _packet_decode, ( const uint8_t* frame, size_t size, libmeshcore_packet_t* packet ));

/** @brief Build a frame. 0, -EINVAL (a field out of its range), -ENOSPC (the frame does not fit in `capacity`); `*size` gets its length. */
dmod_libmeshcore_api(1.0, int, _packet_encode, ( const libmeshcore_packet_t* packet, uint8_t* frame, size_t capacity, size_t* size ));

/** @brief The name of a route / payload type / node type ("flood", "advert", "repeater"), "?" for what is not known. */
dmod_libmeshcore_api(1.0, const char*, _route_name, ( uint8_t route ));
dmod_libmeshcore_api(1.0, const char*, _type_name, ( uint8_t type ));
dmod_libmeshcore_api(1.0, const char*, _node_type_name, ( uint8_t node_type ));

/** @brief The advert of a packet of type LIBMESHCORE_TYPE_ADVERT. 0, -EINVAL (not an advert, or a payload that is too short or has a name that does not fit). */
dmod_libmeshcore_api(1.0, int, _advert_decode, ( const libmeshcore_packet_t* packet, libmeshcore_advert_t* advert ));

/** @brief A channel from its 16-byte secret. */
dmod_libmeshcore_api(1.0, int, _channel_init, ( libmeshcore_channel_t* channel, const uint8_t* secret ));

/** @brief A channel from the secret in base64 (as the apps show it: "izOH6cXN6mrJ5e26oRXNcg==" is the public channel), in 32 hex digits, or from a name that begins with '#' ("#test": the key is made of the name). 0 or -EINVAL. */
dmod_libmeshcore_api(1.0, int, _channel_from_text, ( libmeshcore_channel_t* channel, const char* text ));

/**
 * @brief Decrypt a text of a group channel. 0; -ENOENT when the packet is of another channel (the hash does not fit: the payload is not for this
 *        channel); -EBADMSG when the hash fits and the MAC does not (the channel's secret is not the one of the sender); -EINVAL (not a group text,
 *        a length that is not the MAC and a multiple of 16 bytes), -EIO (the crypto is not there).
 */
dmod_libmeshcore_api(1.0, int, _group_text_decode, ( const libmeshcore_channel_t* channel, const libmeshcore_packet_t* packet, libmeshcore_group_text_t* text ));

/** @brief Build the payload of a group text: encrypted, with the channel hash and the MAC; `packet->type` becomes LIBMESHCORE_TYPE_GRP_TXT. 0, -EINVAL, -EIO. */
dmod_libmeshcore_api(1.0, int, _group_text_encode, ( const libmeshcore_channel_t* channel, const libmeshcore_group_text_t* text, libmeshcore_packet_t* packet ));


/* ---------------------------------------------------------------------------------------------------------------------------------------- */
/* The service: the radio, what it heard, the settings                                                                                     */
/* ---------------------------------------------------------------------------------------------------------------------------------------- */

#define LIBMESHCORE_NODE_NAME_MAX   31      /**< The name of this node (without the terminator). */
#define LIBMESHCORE_MAX_CONTACTS    16      /**< The nodes it remembers (the oldest is forgotten). */
#define LIBMESHCORE_MAX_MESSAGES    32      /**< The texts it remembers. */
#define LIBMESHCORE_MAX_PACKETS     16      /**< The packets of the log. */
#define LIBMESHCORE_MAX_CHANNELS    8       /**< The channels it listens to (the public one is the first). */
#define LIBMESHCORE_CHANNEL_NAME_MAX 15
#define LIBMESHCORE_MESSAGE_TEXT_MAX 120

/** The presets of the radio (the settings of the apps of MeshCore): LIBMESHCORE_PRESET_CUSTOM keeps the numbers of the settings as they are. */
typedef enum
{
    LIBMESHCORE_PRESET_EU_NARROW = 0,       /**< EU/UK narrow: 869.618 MHz, 62.5 kHz, SF8, 4/8. */
    LIBMESHCORE_PRESET_EU_LONG,             /**< EU/UK long range: 869.525 MHz, 250 kHz, SF11, 4/5. */
    LIBMESHCORE_PRESET_US,                  /**< USA/Canada: 910.525 MHz, 62.5 kHz, SF7, 4/5. */
    LIBMESHCORE_PRESET_CUSTOM,
    LIBMESHCORE_PRESET_COUNT
} libmeshcore_preset_t;

/** What a preset is: LIBMESHCORE_PRESET_*. */
typedef struct
{
    char        name[24];
    uint32_t    frequency_hz;
    uint32_t    bandwidth_hz;
    uint8_t     spreading_factor;
    uint8_t     coding_rate;                /**< 5..8 (4/5 .. 4/8). */
} libmeshcore_preset_info_t;

/** The settings of the mesh node; kept on the device (/ext/meshcore.ini), applied to the radio at once. */
typedef struct
{
    bool        enabled;                    /**< false: the service does not listen. */
    uint8_t     preset;                     /**< libmeshcore_preset_t; the numbers below follow it unless it is CUSTOM. */
    uint32_t    frequency_hz;
    uint32_t    bandwidth_hz;
    uint8_t     spreading_factor;
    uint8_t     coding_rate;
    int8_t      tx_power_dbm;
    uint8_t     path_hash_size;             /**< The bytes of a hash in the path of what this node sends (1..3): the repeaters add their hashes of this size. */
    char        name[LIBMESHCORE_NODE_NAME_MAX + 1];    /**< The name of this node in the mesh. */
} libmeshcore_settings_t;

/** The service now. */
typedef struct
{
    bool        running;                    /**< The service has a radio. */
    bool        listening;                  /**< ... and it listens (enabled, the radio is ready). */
    uint32_t    serial;                     /**< Grows at every change of what the service keeps: libmeshcore_wait() tells when. */
    uint32_t    packets;                    /**< The packets heard (MeshCore ones). */
    uint32_t    foreign;                    /**< The frames that were not MeshCore packets. */
    uint32_t    adverts;
    uint32_t    texts;                      /**< The texts of the channels it has the key of. */
    uint32_t    sent;
    int16_t     last_rssi_dbm;
    char        radio[24];                  /**< The chip ("SX1262"). */
} libmeshcore_status_t;

/** A node that was heard (its advert). */
typedef struct
{
    uint8_t     public_key[LIBMESHCORE_PUBLIC_KEY_SIZE];
    char        name[LIBMESHCORE_NAME_MAX + 1];
    uint8_t     node_type;                  /**< libmeshcore_node_type_t. */
    uint8_t     hops;                       /**< The hops the last advert made. */
    bool        has_location;
    int32_t     latitude_e6;
    int32_t     longitude_e6;
    int16_t     rssi_dbm;                   /**< Of the last advert. */
    uint32_t    age_s;                      /**< Seconds since the last advert. */
} libmeshcore_contact_t;

/** A text of a channel. */
typedef struct
{
    uint32_t    timestamp;                  /**< By the clock of the sender. */
    uint32_t    heard_utc;                  /**< When it was heard: seconds since 1970 (UTC) by the clock of this node; 0 when the clock was not set (yet). */
    uint32_t    age_s;                      /**< Seconds since it was heard. */
    char        channel[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
    char        text[LIBMESHCORE_MESSAGE_TEXT_MAX + 1];     /**< "sender: message". */
    int16_t     rssi_dbm;
    uint8_t     hops;                       /**< The hops it made (the repeaters in its path); of a text heard twice - the fewer. */
    bool        mine;                       /**< Sent by this node. */
} libmeshcore_message_t;

/** One line of the log of the packets heard. */
typedef struct
{
    uint32_t    age_s;
    uint8_t     route, type, hops, channel_hash;     /**< channel_hash: of a group packet, else 0. */
    uint8_t     length;
    int16_t     rssi_dbm;
    int8_t      snr_db;
} libmeshcore_log_t;

/** @brief The service now. 0 (even with no radio: `running` is false). */
dmod_libmeshcore_api(1.0, int, _status, ( libmeshcore_status_t* status ));

/** @brief Wait until something changed (the serial of the status is not `serial`) or the time is up; returns the serial now. */
dmod_libmeshcore_api(1.0, uint32_t, _wait, ( uint32_t serial, uint32_t timeout_ms ));

/** @brief The settings. */
dmod_libmeshcore_api(1.0, int, _settings_get, ( libmeshcore_settings_t* settings ));

/**
 * @brief Change the settings: checked, kept on the device, applied to the radio. A preset other than CUSTOM sets the numbers. 0, -EINVAL (a number
 *        the radio cannot take, an empty name), -EIO (the file cannot be written: the settings still hold until the restart).
 */
dmod_libmeshcore_api(1.0, int, _settings_set, ( const libmeshcore_settings_t* settings ));

/** @brief The preset `index` (0 .. LIBMESHCORE_PRESET_COUNT - 1; CUSTOM has name "Custom" and zeros). -EINVAL when the index is out of range. */
dmod_libmeshcore_api(1.0, int, _preset, ( uint8_t index, libmeshcore_preset_info_t* preset ));

/** @brief The nodes heard, the most recent first; `*count` gets how many there are (at most `capacity`). */
dmod_libmeshcore_api(1.0, int, _contacts, ( libmeshcore_contact_t* contacts, size_t capacity, size_t* count ));

/** @brief The texts, the most recent first. */
dmod_libmeshcore_api(1.0, int, _messages, ( libmeshcore_message_t* messages, size_t capacity, size_t* count ));

/** @brief The log of the packets heard, the most recent first. */
dmod_libmeshcore_api(1.0, int, _log, ( libmeshcore_log_t* log, size_t capacity, size_t* count ));

/**
 * @brief Listen to one more channel (from its secret: base64 or 32 hex digits; the name is for the screen). A name that begins with '#' needs no secret
 *        (NULL or ""): "#test" is the channel everybody gets who types "#test". The channels are kept in /ext/meshcore-channels.ini.
 *        0, -EINVAL, -ENOSPC (all the places are taken), -EEXIST.
 */
dmod_libmeshcore_api(1.0, int, _channel_add, ( const char* name, const char* secret ));

/** @brief Stop listening to the channel `index` (not the public one, 0). 0, -EINVAL, -ENOENT. The texts that were heard stay. */
dmod_libmeshcore_api(1.0, int, _channel_remove, ( uint8_t index ));

/**
 * @brief Find a link that adds a channel in a text: "meshcore://channel/add?name=Friends&secret=<32 hex digits or base64>" (what the apps of MeshCore share;
 *        "%20" and "+" in the name are a space). `name` and `secret` get the values (as they are in the link: the secret is not checked, _channel_add does).
 *        0; -ENOENT (no link in the text, or it has no name or no secret); -EINVAL; -ENOSPC (a value does not fit).
 */
dmod_libmeshcore_api(1.0, int, _channel_link_parse, ( const char* text, char* name, size_t name_size, char* secret, size_t secret_size ));

/** @brief The index of the channel that has this secret (base64, 32 hex digits or a name with '#'), when it is listened to: 0 and `index`; -ENOENT; -EINVAL. */
dmod_libmeshcore_api(1.0, int, _channel_find, ( const char* secret, uint8_t* index ));

/** @brief The channels: `name` of the channel `index` (0 is the public one); -ENOENT when there is none. */
dmod_libmeshcore_api(1.0, int, _channel_name, ( uint8_t index, char* name, size_t capacity, uint8_t* hash ));

/**
 * @brief Send a text to a channel (a flood; the text goes as "<name of this node>: <text>"). It looks at the channel first (CAD: if somebody
 *        sends it waits a little and tries again). 0, -ENODEV (no radio), -EINVAL, -ENOENT (no such channel), -EBUSY (the channel stays busy), or the
 *        error of the radio.
 */
dmod_libmeshcore_api(1.0, int, _send_text, ( uint8_t channel_index, const char* text ));

#endif // LIBMESHCORE_H
