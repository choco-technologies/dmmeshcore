#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmmeshcore.h"
#include <errno.h>
#include <string.h>

#define HEADER_ROUTE_MASK       0x03u
#define HEADER_TYPE_SHIFT       2u
#define HEADER_TYPE_MASK        0x0Fu
#define HEADER_VERSION_SHIFT    6u
#define PATH_COUNT_MASK         0x3Fu
#define PATH_SIZE_SHIFT         6u

#define ADVERT_TYPE_MASK        0x0Fu
#define ADVERT_LATLON           0x10u
#define ADVERT_FEATURE1         0x20u
#define ADVERT_FEATURE2         0x40u
#define ADVERT_NAME             0x80u

static bool route_has_transport(uint8_t route)
{
    return route == DMMESHCORE_ROUTE_TRANSPORT_FLOOD || route == DMMESHCORE_ROUTE_TRANSPORT_DIRECT;
}

dmod_dmmeshcore_api_declaration(1.0, int, _packet_decode, ( const uint8_t* frame, size_t size, dmmeshcore_packet_t* packet ))
{
    if (frame == NULL || packet == NULL)
        return -EINVAL;
    memset(packet, 0, sizeof(*packet));
    if (size < 2u || size > DMMESHCORE_MAX_FRAME)
        return -EINVAL;
    size_t at = 0;
    uint8_t header = frame[at++];
    packet->route = (uint8_t)(header & HEADER_ROUTE_MASK);
    packet->type = (uint8_t)((header >> HEADER_TYPE_SHIFT) & HEADER_TYPE_MASK);
    packet->version = (uint8_t)(header >> HEADER_VERSION_SHIFT);
    if (route_has_transport(packet->route))
    {
        if (size < at + 5u)
            return -EINVAL;
        packet->transport_codes[0] = (uint16_t)(frame[at] | (frame[at + 1u] << 8));
        packet->transport_codes[1] = (uint16_t)(frame[at + 2u] | (frame[at + 3u] << 8));
        at += 4u;
    }
    uint8_t path_length = frame[at++];
    packet->hash_count = (uint8_t)(path_length & PATH_COUNT_MASK);
    packet->hash_size = (uint8_t)((path_length >> PATH_SIZE_SHIFT) + 1u);
    if (packet->hash_size > 3u)
        return -EINVAL;                                 /* (the code 3 is reserved) */
    size_t path_bytes = (size_t)packet->hash_count * packet->hash_size;
    if (path_bytes > DMMESHCORE_MAX_PATH || size < at + path_bytes)
        return -EINVAL;
    memcpy(packet->path, frame + at, path_bytes);
    at += path_bytes;
    size_t payload = size - at;
    if (payload > DMMESHCORE_MAX_PAYLOAD)
        return -EMSGSIZE;
    memcpy(packet->payload, frame + at, payload);
    packet->payload_length = (uint8_t)payload;
    return 0;
}

dmod_dmmeshcore_api_declaration(1.0, int, _packet_encode, ( const dmmeshcore_packet_t* packet, uint8_t* frame, size_t capacity, size_t* size ))
{
    if (packet == NULL || frame == NULL || size == NULL)
        return -EINVAL;
    if (packet->route > 3u || packet->type > HEADER_TYPE_MASK || packet->version > 3u || packet->hash_size < 1u || packet->hash_size > 3u ||
        packet->hash_count > PATH_COUNT_MASK || packet->payload_length > DMMESHCORE_MAX_PAYLOAD)
        return -EINVAL;
    size_t path_bytes = (size_t)packet->hash_count * packet->hash_size;
    if (path_bytes > DMMESHCORE_MAX_PATH)
        return -EINVAL;
    size_t length = 2u + (route_has_transport(packet->route) ? 4u : 0u) + path_bytes + packet->payload_length;
    if (length > capacity || length > DMMESHCORE_MAX_FRAME)
        return -ENOSPC;
    size_t at = 0;
    frame[at++] = (uint8_t)(packet->route | (packet->type << HEADER_TYPE_SHIFT) | (packet->version << HEADER_VERSION_SHIFT));
    if (route_has_transport(packet->route))
    {
        frame[at++] = (uint8_t)packet->transport_codes[0];
        frame[at++] = (uint8_t)(packet->transport_codes[0] >> 8);
        frame[at++] = (uint8_t)packet->transport_codes[1];
        frame[at++] = (uint8_t)(packet->transport_codes[1] >> 8);
    }
    frame[at++] = (uint8_t)(packet->hash_count | ((packet->hash_size - 1u) << PATH_SIZE_SHIFT));
    memcpy(frame + at, packet->path, path_bytes);
    at += path_bytes;
    memcpy(frame + at, packet->payload, packet->payload_length);
    at += packet->payload_length;
    *size = at;
    return 0;
}

dmod_dmmeshcore_api_declaration(1.0, const char*, _route_name, ( uint8_t route ))
{
    switch (route)
    {
        case DMMESHCORE_ROUTE_TRANSPORT_FLOOD:  return "transport-flood";
        case DMMESHCORE_ROUTE_FLOOD:            return "flood";
        case DMMESHCORE_ROUTE_DIRECT:           return "direct";
        case DMMESHCORE_ROUTE_TRANSPORT_DIRECT: return "transport-direct";
        default:                                return "?";
    }
}

dmod_dmmeshcore_api_declaration(1.0, const char*, _type_name, ( uint8_t type ))
{
    switch (type)
    {
        case DMMESHCORE_TYPE_REQ:        return "req";
        case DMMESHCORE_TYPE_RESPONSE:   return "response";
        case DMMESHCORE_TYPE_TXT_MSG:    return "text";
        case DMMESHCORE_TYPE_ACK:        return "ack";
        case DMMESHCORE_TYPE_ADVERT:     return "advert";
        case DMMESHCORE_TYPE_GRP_TXT:    return "group-text";
        case DMMESHCORE_TYPE_GRP_DATA:   return "group-data";
        case DMMESHCORE_TYPE_ANON_REQ:   return "anon-req";
        case DMMESHCORE_TYPE_PATH:       return "path";
        case DMMESHCORE_TYPE_TRACE:      return "trace";
        case DMMESHCORE_TYPE_MULTIPART:  return "multipart";
        case DMMESHCORE_TYPE_CONTROL:    return "control";
        case DMMESHCORE_TYPE_RAW_CUSTOM: return "raw";
        default:                         return "?";
    }
}

dmod_dmmeshcore_api_declaration(1.0, const char*, _node_type_name, ( uint8_t node_type ))
{
    switch (node_type)
    {
        case DMMESHCORE_NODE_CHAT:      return "companion";
        case DMMESHCORE_NODE_REPEATER:  return "repeater";
        case DMMESHCORE_NODE_ROOM:      return "room";
        case DMMESHCORE_NODE_SENSOR:    return "sensor";
        default:                        return "?";
    }
}

static int32_t read_i32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

dmod_dmmeshcore_api_declaration(1.0, int, _advert_decode, ( const dmmeshcore_packet_t* packet, dmmeshcore_advert_t* advert ))
{
    if (packet == NULL || advert == NULL || packet->type != DMMESHCORE_TYPE_ADVERT)
        return -EINVAL;
    memset(advert, 0, sizeof(*advert));
    const size_t fixed = DMMESHCORE_PUBLIC_KEY_SIZE + 4u + DMMESHCORE_SIGNATURE_SIZE;
    if (packet->payload_length < fixed)
        return -EINVAL;
    const uint8_t* p = packet->payload;
    memcpy(advert->public_key, p, DMMESHCORE_PUBLIC_KEY_SIZE);
    advert->timestamp = (uint32_t)read_i32(p + DMMESHCORE_PUBLIC_KEY_SIZE);
    memcpy(advert->signature, p + DMMESHCORE_PUBLIC_KEY_SIZE + 4u, DMMESHCORE_SIGNATURE_SIZE);
    size_t at = fixed;
    if (at == packet->payload_length)
        return 0;                                       /* an advert with no app data: only who it is */
    uint8_t flags = p[at++];
    advert->node_type = (uint8_t)(flags & ADVERT_TYPE_MASK);
    if ((flags & ADVERT_LATLON) != 0)
    {
        if (packet->payload_length < at + 8u)
            return -EINVAL;
        advert->has_location = true;
        advert->latitude_e6 = read_i32(p + at);
        advert->longitude_e6 = read_i32(p + at + 4u);
        at += 8u;
    }
    if ((flags & ADVERT_FEATURE1) != 0)
        at += 2u;
    if ((flags & ADVERT_FEATURE2) != 0)
        at += 2u;
    if (at > packet->payload_length)
        return -EINVAL;
    if ((flags & ADVERT_NAME) != 0)
    {
        size_t name = packet->payload_length - at;
        if (name > DMMESHCORE_NAME_MAX)
            return -EINVAL;
        memcpy(advert->name, p + at, name);
        advert->name[name] = '\0';
    }
    return 0;
}

int dmod_init( const Dmod_Config_t* Config )
{
    (void)Config;
    return 0;
}

int dmod_deinit( void )
{
    return 0;
}
