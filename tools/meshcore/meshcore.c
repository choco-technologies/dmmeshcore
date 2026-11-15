#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmmeshcore.h"
#include <errno.h>
#include <string.h>

/* meshcore [-d NODE] [-k KEY]... sniff [SECONDS]
 *   listens on a LoRa node (/dev/lora) and says what MeshCore packets come: the route, the type, the hops, adverts (who), group texts of the
 *   channels it has the key of (KEY: base64 or 32 hex digits; the public channel is always tried) */

#define MAX_CHANNELS 4

typedef struct
{
    dmmeshcore_channel_t channel[MAX_CHANNELS];
    unsigned count;
} channels_t;

static bool parse_number(const char* text, long* value)
{
    long v = 0;
    if (*text == '\0')
        return false;
    for (; *text != '\0'; text++)
    {
        if (*text < '0' || *text > '9')
            return false;
        v = v * 10 + (*text - '0');
    }
    *value = v;
    return true;
}

static void print_advert(const dmmeshcore_packet_t* packet)
{
    dmmeshcore_advert_t advert;
    if (dmmeshcore_advert_decode(packet, &advert) != 0)
    {
        Dmod_Printf("    (an advert that cannot be read)\n");
        return;
    }
    Dmod_Printf("    %s \"%s\"  key %02x%02x%02x%02x..  time %u", dmmeshcore_node_type_name(advert.node_type), advert.name, (unsigned)advert.public_key[0],
                (unsigned)advert.public_key[1], (unsigned)advert.public_key[2], (unsigned)advert.public_key[3], (unsigned)advert.timestamp);
    if (advert.has_location)
        Dmod_Printf("  at %d.%06d, %d.%06d", (int)(advert.latitude_e6 / 1000000), (int)(advert.latitude_e6 < 0 ? -advert.latitude_e6 : advert.latitude_e6) % 1000000,
                    (int)(advert.longitude_e6 / 1000000), (int)(advert.longitude_e6 < 0 ? -advert.longitude_e6 : advert.longitude_e6) % 1000000);
    Dmod_Printf("\n");
}

static void print_group_text(const channels_t* channels, const dmmeshcore_packet_t* packet)
{
    if (packet->payload_length < 1)
        return;
    for (unsigned i = 0; i < channels->count; i++)
    {
        dmmeshcore_group_text_t text;
        int ret = dmmeshcore_group_text_decode(&channels->channel[i], packet, &text);
        if (ret == 0)
        {
            Dmod_Printf("    #%02x: %s\n", (unsigned)channels->channel[i].hash, text.text);
            return;
        }
        if (ret == -EBADMSG)
        {
            Dmod_Printf("    #%02x: the MAC does not fit (another secret)\n", (unsigned)channels->channel[i].hash);
            return;
        }
    }
    Dmod_Printf("    channel #%02x (no key)\n", (unsigned)packet->payload[0]);
}

static void print_packet(const channels_t* channels, const dmdrvi_lora_frame_t* frame)
{
    dmmeshcore_packet_t packet;
    int ret = dmmeshcore_packet_decode(frame->data, frame->length, &packet);
    Dmod_Printf("%d dBm snr %d  %u bytes  ", (int)frame->rssi_dbm, (int)(frame->snr_dbx4 / 4), (unsigned)frame->length);
    if (ret != 0)
    {
        Dmod_Printf("not a MeshCore packet (%d)\n", ret);
        return;
    }
    Dmod_Printf("%s %s v%u  %u hops (%u-byte hashes", dmmeshcore_route_name(packet.route), dmmeshcore_type_name(packet.type), (unsigned)packet.version,
                (unsigned)packet.hash_count, (unsigned)packet.hash_size);
    if (packet.hash_count != 0)
    {
        Dmod_Printf(": ");
        for (unsigned i = 0; i < packet.hash_count; i++)
        {
            for (unsigned b = 0; b < packet.hash_size; b++)
                Dmod_Printf("%02x", (unsigned)packet.path[i * packet.hash_size + b]);
            Dmod_Printf(i + 1 < packet.hash_count ? " " : "");
        }
    }
    Dmod_Printf(")  payload %u\n", (unsigned)packet.payload_length);
    if (packet.type == DMMESHCORE_TYPE_ADVERT)
        print_advert(&packet);
    else if (packet.type == DMMESHCORE_TYPE_GRP_TXT)
        print_group_text(channels, &packet);
}

static int sniff(void* node, const channels_t* channels, long seconds)
{
    unsigned count = 0;
    for (long t = 0; t < seconds; t++)
    {
        dmdrvi_lora_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.timeout_ms = 1000;
        int ret = Dmod_Ioctl(node, DMDRVI_IOCTL_LORA_RECV, &frame);
        if (ret == -ETIMEDOUT || ret == -EAGAIN)
            continue;
        if (ret != 0)
            return ret;
        print_packet(channels, &frame);
        count++;
    }
    Dmod_Printf("%u packets in %ld s\n", count, seconds);
    return 0;
}

int main(int argc, char* argv[])
{
    const char* path = "/dev/lora";
    channels_t channels;
    memset(&channels, 0, sizeof(channels));
    (void)dmmeshcore_channel_from_text(&channels.channel[channels.count++], "izOH6cXN6mrJ5e26oRXNcg==");      /* the public channel */
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i += 2)
    {
        if (i + 1 >= argc)
            break;
        if (strcmp(argv[i], "-d") == 0)
            path = argv[i + 1];
        else if (strcmp(argv[i], "-k") == 0 && channels.count < MAX_CHANNELS)
        {
            if (dmmeshcore_channel_from_text(&channels.channel[channels.count], argv[i + 1]) != 0)
            {
                Dmod_Printf("meshcore: the key must be base64 or 32 hex digits\n");
                return 1;
            }
            channels.count++;
        }
    }
    if (i >= argc || strcmp(argv[i], "sniff") != 0)
    {
        Dmod_Printf("usage: meshcore [-d NODE] [-k KEY]... sniff [SECONDS]\n");
        return 1;
    }
    long seconds = 30;
    if (i + 1 < argc && !parse_number(argv[i + 1], &seconds))
        return 1;
    void* node = Dmod_FileOpen(path, "r+");
    if (node == NULL)
    {
        Dmod_Printf("meshcore: cannot open %s\n", path);
        return 1;
    }
    int ret = sniff(node, &channels, seconds);
    Dmod_FileClose(node);
    if (ret != 0)
        Dmod_Printf("meshcore: error %d\n", ret);
    return ret == 0 ? 0 : 1;
}
