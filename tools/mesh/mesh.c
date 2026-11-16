#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "libmeshcore.h"
#include <errno.h>
#include <string.h>

/* mesh: a thin shell over libmeshcore - everything is in the library
 *   mesh [status]                 the service and the counters
 *   mesh nodes | texts | log      what was heard
 *   mesh settings                 the settings
 *   mesh set KEY=VALUE...         preset (0..3), frequency, bandwidth, sf, cr, power, name, enabled
 *   mesh presets                  the presets
 *   mesh channel NAME [KEY]       listen to a channel (KEY: base64 or 32 hex digits; none for a name that begins with '#', like "#test")
 *   mesh unchannel INDEX          stop listening to a channel (not the public one)
 *   mesh send CHANNEL TEXT        send a text (a flood) to a channel
 *   mesh watch [SECONDS]          print what comes */

static bool number(const char* text, long* value)
{
    bool negative = text[0] == '-';
    long v = 0;
    const char* c = text + (negative ? 1 : 0);
    if (*c == '\0')
        return false;
    for (; *c != '\0'; c++)
    {
        if (*c < '0' || *c > '9')
            return false;
        v = v * 10 + (*c - '0');
    }
    *value = negative ? -v : v;
    return true;
}

static void print_status(void)
{
    libmeshcore_status_t s;
    (void)libmeshcore_status(&s);
    Dmod_Printf("%s  radio %s  packets %u (foreign %u)  adverts %u  texts %u  sent %u  last %d dBm\n",
                !s.running ? "no radio" : s.listening ? "listening" : "idle", s.radio[0] != '\0' ? s.radio : "-", (unsigned)s.packets, (unsigned)s.foreign,
                (unsigned)s.adverts, (unsigned)s.texts, (unsigned)s.sent, (int)s.last_rssi_dbm);
}

static void print_settings(void)
{
    libmeshcore_settings_t s;
    libmeshcore_preset_info_t p;
    (void)libmeshcore_settings_get(&s);
    (void)libmeshcore_preset(s.preset, &p);
    Dmod_Printf("%s  preset %u (%s)  %u Hz  %u Hz  SF%u  4/%u  %d dBm  name \"%s\"\n", s.enabled ? "on" : "off", (unsigned)s.preset, p.name,
                (unsigned)s.frequency_hz, (unsigned)s.bandwidth_hz, (unsigned)s.spreading_factor, (unsigned)s.coding_rate, (int)s.tx_power_dbm, s.name);
}

static int set_values(int argc, char** argv)
{
    libmeshcore_settings_t s;
    (void)libmeshcore_settings_get(&s);
    for (int i = 0; i < argc; i++)
    {
        const char* eq = strchr(argv[i], '=');
        long v = 0;
        if (eq == NULL)
            return -EINVAL;
        size_t k = (size_t)(eq - argv[i]);
        if (k == 4 && strncmp(argv[i], "name", 4) == 0)
        {
            Dmod_SnPrintf(s.name, sizeof(s.name), "%s", eq + 1);
            continue;
        }
        if (!number(eq + 1, &v))
            return -EINVAL;
        if (k == 6 && strncmp(argv[i], "preset", 6) == 0) s.preset = (uint8_t)v;
        else if (k == 9 && strncmp(argv[i], "frequency", 9) == 0) { s.frequency_hz = (uint32_t)v; s.preset = LIBMESHCORE_PRESET_CUSTOM; }
        else if (k == 9 && strncmp(argv[i], "bandwidth", 9) == 0) { s.bandwidth_hz = (uint32_t)v; s.preset = LIBMESHCORE_PRESET_CUSTOM; }
        else if (k == 2 && strncmp(argv[i], "sf", 2) == 0) { s.spreading_factor = (uint8_t)v; s.preset = LIBMESHCORE_PRESET_CUSTOM; }
        else if (k == 2 && strncmp(argv[i], "cr", 2) == 0) { s.coding_rate = (uint8_t)v; s.preset = LIBMESHCORE_PRESET_CUSTOM; }
        else if (k == 5 && strncmp(argv[i], "power", 5) == 0) s.tx_power_dbm = (int8_t)v;
        else if (k == 7 && strncmp(argv[i], "enabled", 7) == 0) s.enabled = v != 0;
        else return -EINVAL;
    }
    int ret = libmeshcore_settings_set(&s);
    print_settings();
    return ret;
}

static void print_node(const libmeshcore_contact_t* c)
{
    Dmod_Printf("%-18s %-9s %u hops  %d dBm  %us ago  %02x%02x%02x%02x", c->name, libmeshcore_node_type_name(c->node_type), (unsigned)c->hops, (int)c->rssi_dbm,
                (unsigned)c->age_s, (unsigned)c->public_key[0], (unsigned)c->public_key[1], (unsigned)c->public_key[2], (unsigned)c->public_key[3]);
    if (c->has_location)
        Dmod_Printf("  %d.%06d, %d.%06d", (int)(c->latitude_e6 / 1000000), (int)(c->latitude_e6 < 0 ? -c->latitude_e6 : c->latitude_e6) % 1000000,
                    (int)(c->longitude_e6 / 1000000), (int)(c->longitude_e6 < 0 ? -c->longitude_e6 : c->longitude_e6) % 1000000);
    Dmod_Printf("\n");
}

static void print_text(const libmeshcore_message_t* m)
{
    Dmod_Printf("[%s] %s%s  (%us ago, %d dBm, %u hops)\n", m->channel, m->mine ? "> " : "", m->text, (unsigned)m->age_s, (int)m->rssi_dbm, (unsigned)m->hops);
}

static int watch(long seconds)
{
    uint32_t serial = 0;
    libmeshcore_status_t s;
    (void)libmeshcore_status(&s);
    serial = s.serial;
    uint32_t packets = s.packets;
    for (long t = 0; t < seconds * 2;)
    {
        uint32_t now = libmeshcore_wait(serial, 500);
        if (now == serial)
        {
            t++;
            continue;
        }
        serial = now;
        (void)libmeshcore_status(&s);
        libmeshcore_log_t log[4];
        size_t count = 0;
        (void)libmeshcore_log(log, sizeof(log) / sizeof(log[0]), &count);
        uint32_t fresh = s.packets - packets;
        packets = s.packets;
        for (size_t i = fresh < count ? fresh : count; i > 0; i--)
            Dmod_Printf("%d dBm snr %d  %u bytes  %s %s  %u hops%s\n", (int)log[i - 1].rssi_dbm, (int)log[i - 1].snr_db, (unsigned)log[i - 1].length,
                        libmeshcore_route_name(log[i - 1].route), libmeshcore_type_name(log[i - 1].type), (unsigned)log[i - 1].hops,
                        log[i - 1].type == LIBMESHCORE_TYPE_GRP_TXT ? " (group text)" : "");
    }
    return 0;
}

int main(int argc, char* argv[])
{
    const char* action = argc < 2 ? "status" : argv[1];
    int ret = 0;
    if (strcmp(action, "status") == 0)
        print_status();
    else if (strcmp(action, "settings") == 0)
        print_settings();
    else if (strcmp(action, "set") == 0)
        ret = set_values(argc - 2, argv + 2);
    else if (strcmp(action, "presets") == 0)
    {
        for (uint8_t i = 0; i < LIBMESHCORE_PRESET_COUNT; i++)
        {
            libmeshcore_preset_info_t p;
            if (libmeshcore_preset(i, &p) == 0)
                Dmod_Printf("%u  %-18s %u Hz  %u Hz  SF%u  4/%u\n", (unsigned)i, p.name, (unsigned)p.frequency_hz, (unsigned)p.bandwidth_hz,
                            (unsigned)p.spreading_factor, (unsigned)p.coding_rate);
        }
    }
    else if (strcmp(action, "nodes") == 0)
    {
        libmeshcore_contact_t* c = Dmod_Malloc(sizeof(*c) * LIBMESHCORE_MAX_CONTACTS);
        size_t n = 0;
        ret = c == NULL ? -ENOMEM : libmeshcore_contacts(c, LIBMESHCORE_MAX_CONTACTS, &n);
        for (size_t i = 0; ret == 0 && i < n; i++)
            print_node(&c[i]);
        if (ret == 0 && n == 0)
            Dmod_Printf("no node heard yet\n");
        Dmod_Free(c);
    }
    else if (strcmp(action, "texts") == 0)
    {
        libmeshcore_message_t* m = Dmod_Malloc(sizeof(*m) * LIBMESHCORE_MAX_MESSAGES);
        size_t n = 0;
        ret = m == NULL ? -ENOMEM : libmeshcore_messages(m, LIBMESHCORE_MAX_MESSAGES, &n);
        for (size_t i = 0; ret == 0 && i < n; i++)
            print_text(&m[i]);
        if (ret == 0 && n == 0)
            Dmod_Printf("no text yet\n");
        Dmod_Free(m);
    }
    else if (strcmp(action, "log") == 0)
    {
        libmeshcore_log_t l[LIBMESHCORE_MAX_PACKETS];
        size_t n = 0;
        ret = libmeshcore_log(l, LIBMESHCORE_MAX_PACKETS, &n);
        for (size_t i = 0; ret == 0 && i < n; i++)
            Dmod_Printf("%us ago  %d dBm snr %d  %u bytes  %s %s  %u hops  channel #%02x\n", (unsigned)l[i].age_s, (int)l[i].rssi_dbm, (int)l[i].snr_db,
                        (unsigned)l[i].length, libmeshcore_route_name(l[i].route), libmeshcore_type_name(l[i].type), (unsigned)l[i].hops, (unsigned)l[i].channel_hash);
    }
    else if (strcmp(action, "channel") == 0 && (argc == 3 || argc == 4))
        ret = libmeshcore_channel_add(argv[2], argc == 4 ? argv[3] : NULL);
    else if (strcmp(action, "unchannel") == 0 && argc == 3)
    {
        long index = 0;
        ret = number(argv[2], &index) ? libmeshcore_channel_remove((uint8_t)index) : -EINVAL;
    }
    else if (strcmp(action, "send") == 0 && argc == 4)
    {
        long index = 0;
        ret = number(argv[2], &index) ? libmeshcore_send_text((uint8_t)index, argv[3]) : -EINVAL;
    }
    else if (strcmp(action, "watch") == 0)
    {
        long seconds = 30;
        if (argc > 2 && !number(argv[2], &seconds))
            return 1;
        ret = watch(seconds);
    }
    else
        ret = -EINVAL;
    if (ret != 0)
        Dmod_Printf("mesh: error %d\nusage: mesh [status|nodes|texts|log|settings|presets|set KEY=VALUE...|channel NAME [KEY]|unchannel INDEX|send CHANNEL TEXT|watch [SECONDS]]\n", ret);
    return ret == 0 ? 0 : 1;
}
