#include "dmod.h"
#include "internal.h"
#include "dmini.h"
#include "libsystemd_service.h"
#include "dmdrvi_ioctl.h"
#include "dmosi.h"
#include "dmutc.h"
#include <errno.h>
#include <string.h>

/*
 * The service of libmeshcore: libsystemd starts it for a LoRa radio ([class=lora] start=libmeshcore@%name of configs/libmeshcore.rules; the
 * reported path of the node is the argument), a thread listens on the radio, takes every frame apart and keeps what it learns: the nodes (the
 * adverts), the texts of the channels it has the key of, a log of the packets. The applications read all of it through the API.
 */

#define RECV_WAIT_MS        500u
#define IDLE_WAIT_MS        300u
#define SEND_TRIES          5u
#define PATH_MAX_LENGTH     63

typedef struct
{
    libmeshcore_contact_t   info;
    uint32_t                heard_ms;           /* Dmod_GetUptime() of the last advert */
    bool                    used;
} contact_t;

typedef struct
{
    libmeshcore_message_t   info;
    uint32_t                heard_ms;
} message_t;

typedef struct
{
    libmeshcore_log_t       info;
    uint32_t                heard_ms;
} log_t;

typedef struct
{
    char                    name[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
    libmeshcore_channel_t   channel;
    bool                    used;
} channel_slot_t;

static struct
{
    dmosi_mutex_t           lock;
    bool                    started;            /* a radio is served */
    bool                    channels_loaded;    /* the file of the channels was read (or written): until then it is not written over */
    uint32_t                channels_tried_ms;  /* when it was tried last (the file system may come up after the service) */
    char                    unit[48];
    char                    path[PATH_MAX_LENGTH + 1];
    char                    radio[24];
    dmosi_thread_t          worker;
    volatile bool           stopping;
    bool                    listening;
    libmeshcore_settings_t  settings;
    uint32_t                serial;
    uint32_t                packets, foreign, adverts, texts, sent;
    int16_t                 last_rssi;
    contact_t               contacts[LIBMESHCORE_MAX_CONTACTS];
    message_t               messages[LIBMESHCORE_MAX_MESSAGES];
    uint8_t                 message_count, message_next;
    log_t                   log[LIBMESHCORE_MAX_PACKETS];
    uint8_t                 log_count, log_next;
    channel_slot_t          channels[LIBMESHCORE_MAX_CHANNELS];
} g;

static void changed(void)                                   /* the lock is held */
{
    g.serial++;
}

/* ---- The radio ---- */

static void config_of(const libmeshcore_settings_t* s, dmdrvi_lora_config_t* c)
{
    memset(c, 0, sizeof(*c));
    c->frequency_hz = s->frequency_hz;
    c->bandwidth_hz = s->bandwidth_hz;
    c->spreading_factor = s->spreading_factor;
    c->coding_rate = s->coding_rate;
    c->tx_power_dbm = s->tx_power_dbm;
    c->crc = 1;
    c->preamble_symbols = 16;
    c->sync_word = 0x1424;                                  /* the private sync word: what MeshCore uses */
}

/* The settings to the radio: a handle of its own (the worker is in a RECV on its handle) */
static int apply_to_radio(const libmeshcore_settings_t* settings)
{
    char path[PATH_MAX_LENGTH + 1];
    dmosi_mutex_lock(g.lock);
    strcpy(path, g.path);
    bool serving = g.started;
    dmosi_mutex_unlock(g.lock);
    if (!serving)
        return 0;
    void* file = Dmod_FileOpen(path, "r+");
    if (file == NULL)
        return -ENODEV;
    dmdrvi_lora_config_t config;
    config_of(settings, &config);
    int ret = Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_SET_CONFIG, &config);
    Dmod_FileClose(file);
    return ret;
}

/* ---- What was heard ---- */

static channel_slot_t* find_channel_by_hash(uint8_t hash, unsigned from)
{
    for (unsigned i = from; i < LIBMESHCORE_MAX_CHANNELS; i++)
        if (g.channels[i].used && g.channels[i].channel.hash == hash)
            return &g.channels[i];
    return NULL;
}

static void note_contact(const libmeshcore_packet_t* packet, const libmeshcore_advert_t* advert, int16_t rssi, uint32_t now)
{
    contact_t* slot = NULL;
    contact_t* oldest = &g.contacts[0];
    for (unsigned i = 0; i < LIBMESHCORE_MAX_CONTACTS; i++)
    {
        contact_t* c = &g.contacts[i];
        if (c->used && memcmp(c->info.public_key, advert->public_key, LIBMESHCORE_PUBLIC_KEY_SIZE) == 0)
        {
            slot = c;
            break;
        }
        if (!c->used)
            oldest = c, slot = NULL;                        /* a free place is the best one to take */
        else if (oldest->used && (now - c->heard_ms) > (now - oldest->heard_ms))
            oldest = c;
    }
    if (slot == NULL)
        slot = oldest;
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->info.public_key, advert->public_key, LIBMESHCORE_PUBLIC_KEY_SIZE);
    memcpy(slot->info.name, advert->name, sizeof(slot->info.name));
    slot->info.node_type = advert->node_type;
    slot->info.hops = packet->hash_count;
    slot->info.has_location = advert->has_location;
    slot->info.latitude_e6 = advert->latitude_e6;
    slot->info.longitude_e6 = advert->longitude_e6;
    slot->info.rssi_dbm = rssi;
    slot->heard_ms = now;
}

/* The time now in seconds since 1970 by the clock of the node (dmutc), 0 when it is not set */
static uint32_t utc_now(void)
{
    dmutc_time_t utc;
    return (dmutc_get(&utc) == 0 && utc.seconds > 0) ? (uint32_t)utc.seconds : 0u;
}

/* true when the text is new (a copy of one that is kept is not) */
static bool note_message(const channel_slot_t* channel, const libmeshcore_group_text_t* text, const libmeshcore_packet_t* packet, int16_t rssi,
                         uint32_t now, bool mine)
{
    /* A flood reaches a node more than once (through other repeaters): the same text of the same channel with the same time of the sender is one message */
    for (unsigned i = 0; i < g.message_count; i++)
    {
        const message_t* old = &g.messages[i];
        if (old->info.timestamp == text->timestamp && strcmp(old->info.channel, channel->name) == 0 && strcmp(old->info.text, text->text) == 0)
        {
            if (packet->hash_count < g.messages[i].info.hops)       /* the copy that came the shortest way says how far the sender is */
            {
                g.messages[i].info.hops = packet->hash_count;
                g.messages[i].info.rssi_dbm = rssi;
            }
            return false;
        }
    }
    message_t* m = &g.messages[g.message_next];
    memset(m, 0, sizeof(*m));
    m->info.timestamp = text->timestamp;
    m->info.heard_utc = utc_now();
    Dmod_SnPrintf(m->info.channel, sizeof(m->info.channel), "%s", channel->name);
    Dmod_SnPrintf(m->info.text, sizeof(m->info.text), "%s", text->text);
    m->info.rssi_dbm = rssi;
    m->info.hops = packet->hash_count;
    m->info.mine = mine;
    m->heard_ms = now;
    g.message_next = (uint8_t)((g.message_next + 1U) % LIBMESHCORE_MAX_MESSAGES);
    if (g.message_count < LIBMESHCORE_MAX_MESSAGES)
        g.message_count++;
    return true;
}

static void note_log(const libmeshcore_packet_t* packet, uint8_t length, int16_t rssi, int16_t snr_dbx4, uint32_t now)
{
    log_t* l = &g.log[g.log_next];
    memset(l, 0, sizeof(*l));
    l->info.route = packet->route;
    l->info.type = packet->type;
    l->info.hops = packet->hash_count;
    l->info.length = length;
    l->info.rssi_dbm = rssi;
    l->info.snr_db = (int8_t)(snr_dbx4 / 4);
    if (packet->type == LIBMESHCORE_TYPE_GRP_TXT && packet->payload_length > 0)
        l->info.channel_hash = packet->payload[0];
    l->heard_ms = now;
    g.log_next = (uint8_t)((g.log_next + 1U) % LIBMESHCORE_MAX_PACKETS);
    if (g.log_count < LIBMESHCORE_MAX_PACKETS)
        g.log_count++;
}

static void take_frame(const dmdrvi_lora_frame_t* frame)
{
    libmeshcore_packet_t packet;
    uint32_t now = Dmod_GetUptime();
    dmosi_mutex_lock(g.lock);
    g.last_rssi = frame->rssi_dbm;
    if (libmeshcore_packet_decode(frame->data, frame->length, &packet) != 0)
    {
        g.foreign++;
        changed();
        dmosi_mutex_unlock(g.lock);
        return;
    }
    g.packets++;
    note_log(&packet, (uint8_t)frame->length, frame->rssi_dbm, frame->snr_dbx4, now);
    if (packet.type == LIBMESHCORE_TYPE_ADVERT)
    {
        libmeshcore_advert_t advert;
        if (libmeshcore_advert_decode(&packet, &advert) == 0)
        {
            note_contact(&packet, &advert, frame->rssi_dbm, now);
            g.adverts++;
        }
    }
    else if (packet.type == LIBMESHCORE_TYPE_GRP_TXT && packet.payload_length > 0)
    {
        for (const channel_slot_t* ch = find_channel_by_hash(packet.payload[0], 0); ch != NULL; ch = find_channel_by_hash(packet.payload[0], (unsigned)(ch - g.channels) + 1U))
        {
            libmeshcore_group_text_t text;
            if (libmeshcore_group_text_decode(&ch->channel, &packet, &text) == 0)
            {
                if (note_message(ch, &text, &packet, frame->rssi_dbm, now, false))
                    g.texts++;
                break;
            }
        }
    }
    changed();
    dmosi_mutex_unlock(g.lock);
}

/* ---- The worker ---- */

static void ensure_channels_loaded_locked(void);

static void worker_main(void* arg)
{
    (void)arg;
    void* file = NULL;
    bool configured = false;
    while (!g.stopping)
    {
        dmosi_mutex_lock(g.lock);
        bool serving = g.started;
        if (serving)
            ensure_channels_loaded_locked();
        bool enabled = g.settings.enabled && serving;
        libmeshcore_settings_t settings = g.settings;
        char path[PATH_MAX_LENGTH + 1];
        strcpy(path, g.path);
        dmosi_mutex_unlock(g.lock);
        if (!enabled)
        {
            if (file != NULL)
            {
                Dmod_FileClose(file);                       /* the radio is not served now (a stop, or the service is off) */
                file = NULL;
            }
            dmosi_mutex_lock(g.lock);
            if (g.listening)
            {
                g.listening = false;
                changed();
            }
            dmosi_mutex_unlock(g.lock);
            dmosi_thread_sleep(IDLE_WAIT_MS);
            continue;
        }
        if (file == NULL)
        {
            file = Dmod_FileOpen(path, "r+");
            configured = false;
            if (file == NULL)
            {
                dmosi_thread_sleep(IDLE_WAIT_MS);
                continue;
            }
        }
        if (!configured)
        {
            dmdrvi_lora_config_t config;
            dmdrvi_lora_info_t info;
            config_of(&settings, &config);
            if (Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_SET_CONFIG, &config) != 0)
            {
                Dmod_FileClose(file);
                file = NULL;
                dmosi_thread_sleep(IDLE_WAIT_MS);
                continue;
            }
            dmosi_mutex_lock(g.lock);
            if (Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_GET_INFO, &info) == 0)
            {
                Dmod_SnPrintf(g.radio, sizeof(g.radio), "%s", info.chip);
            }
            g.listening = true;
            changed();
            dmosi_mutex_unlock(g.lock);
            configured = true;
        }
        dmdrvi_lora_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.timeout_ms = RECV_WAIT_MS;
        int ret = Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_RECV, &frame);
        if (ret == 0)
            take_frame(&frame);
        else if (ret != -ETIMEDOUT && ret != -EAGAIN)
        {
            Dmod_FileClose(file);                           /* the radio is gone (or not ready yet): open it again */
            file = NULL;
            dmosi_mutex_lock(g.lock);
            g.listening = false;
            dmosi_mutex_unlock(g.lock);
            dmosi_thread_sleep(IDLE_WAIT_MS);
        }
    }
    if (file != NULL)
        Dmod_FileClose(file);
}

static void load_channels_locked(void);

/* ---- The library service (libsystemd) ---- */

int libmeshcore_service_begin( const char* unit, int argc, const char* const argv[] )
{
    if (unit == NULL || argc != 2 || argv == NULL || argv[1] == NULL || argv[1][0] != '/' || strlen(argv[1]) > PATH_MAX_LENGTH || strlen(unit) >= sizeof(g.unit))
        return -EINVAL;
    if (g.lock == NULL)
        return -ENODEV;
    dmosi_mutex_lock(g.lock);
    if (g.started)
    {
        dmosi_mutex_unlock(g.lock);
        return -EBUSY;                                      /* one radio is served */
    }
    strcpy(g.unit, unit);
    strcpy(g.path, argv[1]);
    (void)libmeshcore_settings_load(&g.settings);
    load_channels_locked();
    g.started = true;                                       /* (the thread of the module takes it from here: it was made when the module was loaded - a thread made
                                                             * here would belong to the process of the start of the unit, which ends with it) */
    changed();
    dmosi_mutex_unlock(g.lock);
    return 0;
}

int libmeshcore_service_end( const char* unit )
{
    if (unit == NULL)
        return -EINVAL;
    dmosi_mutex_lock(g.lock);
    bool ours = g.started && strcmp(g.unit, unit) == 0;
    dmosi_mutex_unlock(g.lock);
    if (!ours)
        return -ENOENT;
    dmosi_mutex_lock(g.lock);
    g.started = false;
    g.listening = false;
    changed();
    dmosi_mutex_unlock(g.lock);
    return 0;
}

/* ---- The API ---- */

dmod_libmeshcore_api_declaration(1.0, int, _status, ( libmeshcore_status_t* status ))
{
    if (status == NULL)
        return -EINVAL;
    memset(status, 0, sizeof(*status));
    if (g.lock == NULL)
        return 0;
    dmosi_mutex_lock(g.lock);
    status->running = g.started;
    status->listening = g.listening;
    status->serial = g.serial;
    status->packets = g.packets;
    status->foreign = g.foreign;
    status->adverts = g.adverts;
    status->texts = g.texts;
    status->sent = g.sent;
    status->last_rssi_dbm = g.last_rssi;
    strcpy(status->radio, g.radio);
    dmosi_mutex_unlock(g.lock);
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, uint32_t, _wait, ( uint32_t serial, uint32_t timeout_ms ))
{
    uint32_t waited = 0;
    for (;;)
    {
        uint32_t now = serial;
        if (g.lock != NULL)
        {
            dmosi_mutex_lock(g.lock);
            now = g.serial;
            dmosi_mutex_unlock(g.lock);
        }
        if (now != serial || waited >= timeout_ms)
            return now;
        dmosi_thread_sleep(50);
        waited += 50;
    }
}

dmod_libmeshcore_api_declaration(1.0, int, _settings_get, ( libmeshcore_settings_t* settings ))
{
    if (settings == NULL || g.lock == NULL)
        return -EINVAL;
    dmosi_mutex_lock(g.lock);
    bool loaded = g.started;
    *settings = g.settings;
    dmosi_mutex_unlock(g.lock);
    if (!loaded)
        (void)libmeshcore_settings_load(settings);          /* no radio yet: what the file says */
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, int, _settings_set, ( const libmeshcore_settings_t* settings ))
{
    if (settings == NULL || g.lock == NULL)
        return -EINVAL;
    libmeshcore_settings_t s = *settings;
    int ret = libmeshcore_settings_check(&s);
    if (ret != 0)
        return ret;
    dmosi_mutex_lock(g.lock);
    g.settings = s;
    changed();
    dmosi_mutex_unlock(g.lock);
    int saved = libmeshcore_settings_save(&s);
    int applied = s.enabled ? apply_to_radio(&s) : 0;
    return applied != 0 ? applied : saved;
}

static uint32_t age_s(uint32_t now, uint32_t then)
{
    return (now - then) / 1000u;
}

dmod_libmeshcore_api_declaration(1.0, int, _contacts, ( libmeshcore_contact_t* contacts, size_t capacity, size_t* count ))
{
    if (contacts == NULL || count == NULL || g.lock == NULL)
        return -EINVAL;
    uint32_t now = Dmod_GetUptime();
    size_t n = 0;
    dmosi_mutex_lock(g.lock);
    for (size_t k = 0; k < capacity; k++)
    {
        /* the most recent first: the one with the smallest age that is not given yet */
        const contact_t* best = NULL;
        for (unsigned i = 0; i < LIBMESHCORE_MAX_CONTACTS; i++)
        {
            const contact_t* c = &g.contacts[i];
            if (!c->used)
                continue;
            bool given = false;
            for (size_t j = 0; j < n; j++)
                if (memcmp(contacts[j].public_key, c->info.public_key, LIBMESHCORE_PUBLIC_KEY_SIZE) == 0)
                    given = true;
            if (!given && (best == NULL || (now - c->heard_ms) < (now - best->heard_ms)))
                best = c;
        }
        if (best == NULL)
            break;
        contacts[n] = best->info;
        contacts[n].age_s = age_s(now, best->heard_ms);
        n++;
    }
    dmosi_mutex_unlock(g.lock);
    *count = n;
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, int, _messages, ( libmeshcore_message_t* messages, size_t capacity, size_t* count ))
{
    if (messages == NULL || count == NULL || g.lock == NULL)
        return -EINVAL;
    uint32_t now = Dmod_GetUptime();
    dmosi_mutex_lock(g.lock);
    size_t n = g.message_count < capacity ? g.message_count : capacity;
    for (size_t i = 0; i < n; i++)
    {
        const message_t* m = &g.messages[(g.message_next + LIBMESHCORE_MAX_MESSAGES - 1U - i) % LIBMESHCORE_MAX_MESSAGES];
        messages[i] = m->info;
        messages[i].age_s = age_s(now, m->heard_ms);
    }
    dmosi_mutex_unlock(g.lock);
    *count = n;
    return 0;
}

dmod_libmeshcore_api_declaration(1.0, int, _log, ( libmeshcore_log_t* log, size_t capacity, size_t* count ))
{
    if (log == NULL || count == NULL || g.lock == NULL)
        return -EINVAL;
    uint32_t now = Dmod_GetUptime();
    dmosi_mutex_lock(g.lock);
    size_t n = g.log_count < capacity ? g.log_count : capacity;
    for (size_t i = 0; i < n; i++)
    {
        const log_t* l = &g.log[(g.log_next + LIBMESHCORE_MAX_PACKETS - 1U - i) % LIBMESHCORE_MAX_PACKETS];
        log[i] = l->info;
        log[i].age_s = age_s(now, l->heard_ms);
    }
    dmosi_mutex_unlock(g.lock);
    *count = n;
    return 0;
}

/* Add a channel to the slots (g.lock is held); 0, -ENOSPC, -EEXIST */
static int add_channel_locked(const char* name, const libmeshcore_channel_t* channel)
{
    for (unsigned i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
        if (g.channels[i].used && memcmp(g.channels[i].channel.secret, channel->secret, sizeof(channel->secret)) == 0)
            return -EEXIST;
    for (unsigned i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
    {
        if (!g.channels[i].used)
        {
            memset(&g.channels[i], 0, sizeof(g.channels[i]));
            Dmod_SnPrintf(g.channels[i].name, sizeof(g.channels[i].name), "%s", name);
            g.channels[i].channel = *channel;
            g.channels[i].used = true;
            changed();
            return 0;
        }
    }
    return -ENOSPC;
}

/* The channels but the public one go to a file of their own: [channels] name<i>=, key<i>= (32 hex digits) */
static void save_channels(void)
{
    dmini_context_t ini = dmini_create();
    if (ini == NULL)
        return;
    char key[24], value[2U * LIBMESHCORE_CHANNEL_KEY_SIZE + 1U];
    unsigned n = 0;
    dmosi_mutex_lock(g.lock);
    for (unsigned i = 1; i < LIBMESHCORE_MAX_CHANNELS; i++)
    {
        if (!g.channels[i].used)
            continue;
        Dmod_SnPrintf(key, sizeof(key), "name%u", n);
        dmini_set_string(ini, "channels", key, g.channels[i].name);
        for (unsigned b = 0; b < LIBMESHCORE_CHANNEL_KEY_SIZE; b++)
            Dmod_SnPrintf(&value[2U * b], 3, "%02x", g.channels[i].channel.secret[b]);
        Dmod_SnPrintf(key, sizeof(key), "key%u", n);
        dmini_set_string(ini, "channels", key, value);
        n++;
    }
    dmosi_mutex_unlock(g.lock);
    dmini_set_int(ini, "channels", "count", (int)n);
    if (dmini_generate_file(ini, LIBMESHCORE_CHANNELS_FILE) == 0)
        g.channels_loaded = true;
    dmini_destroy(ini);
}

/* (g.lock is held) */
static void load_channels_locked(void)
{
    dmini_context_t ini = dmini_create();
    if (ini == NULL)
        return;
    if (dmini_parse_file(ini, LIBMESHCORE_CHANNELS_FILE) == 0 && dmini_has_section(ini, "channels"))
    {
        g.channels_loaded = true;
        int count = dmini_get_int(ini, "channels", "count", 0);
        for (int i = 0; i < count && i < (int)LIBMESHCORE_MAX_CHANNELS; i++)
        {
            char key[24];
            Dmod_SnPrintf(key, sizeof(key), "name%d", i);
            const char* name = dmini_get_string(ini, "channels", key, NULL);
            Dmod_SnPrintf(key, sizeof(key), "key%d", i);
            const char* secret = dmini_get_string(ini, "channels", key, NULL);
            libmeshcore_channel_t channel;
            if (name != NULL && secret != NULL && name[0] != '\0' && libmeshcore_channel_from_text(&channel, secret) == 0)
                (void)add_channel_locked(name, &channel);
        }
    }
    dmini_destroy(ini);
}

/* The service may start before the file system of /ext is there: the file is read at the first use after it is (every 2 s at most), and what is
 * written before it was read would take the channels of the file away - so a channel that is added meanwhile waits for it */
static void ensure_channels_loaded_locked(void)
{
    uint32_t now = Dmod_GetUptime();
    if (g.channels_loaded || (g.channels_tried_ms != 0u && now - g.channels_tried_ms < 2000u))
        return;
    g.channels_tried_ms = now != 0u ? now : 1u;
    load_channels_locked();
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_add, ( const char* name, const char* secret ))
{
    if (name == NULL || name[0] == '\0' || g.lock == NULL)
        return -EINVAL;
    if (secret == NULL || secret[0] == '\0')
        secret = name;                                          /* "#test": the key is made of the name */
    libmeshcore_channel_t channel;
    int ret = libmeshcore_channel_from_text(&channel, secret);
    if (ret != 0)
        return ret;
    dmosi_mutex_lock(g.lock);
    ensure_channels_loaded_locked();
    ret = add_channel_locked(name, &channel);
    dmosi_mutex_unlock(g.lock);
    if (ret == 0)
        save_channels();
    return ret;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_remove, ( uint8_t index ))
{
    if (index == 0 || index >= LIBMESHCORE_MAX_CHANNELS || g.lock == NULL)
        return -EINVAL;
    dmosi_mutex_lock(g.lock);
    ensure_channels_loaded_locked();
    int ret = g.channels[index].used ? 0 : -ENOENT;
    if (ret == 0)
    {
        memset(&g.channels[index], 0, sizeof(g.channels[index]));
        changed();
    }
    dmosi_mutex_unlock(g.lock);
    if (ret == 0)
        save_channels();
    return ret;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_find, ( const char* secret, uint8_t* index ))
{
    libmeshcore_channel_t channel;
    if (secret == NULL || index == NULL || g.lock == NULL || libmeshcore_channel_from_text(&channel, secret) != 0)
        return -EINVAL;
    int ret = -ENOENT;
    dmosi_mutex_lock(g.lock);
    ensure_channels_loaded_locked();
    for (unsigned i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
    {
        if (g.channels[i].used && memcmp(g.channels[i].channel.secret, channel.secret, sizeof(channel.secret)) == 0)
        {
            *index = (uint8_t)i;
            ret = 0;
            break;
        }
    }
    dmosi_mutex_unlock(g.lock);
    return ret;
}

dmod_libmeshcore_api_declaration(1.0, int, _channel_name, ( uint8_t index, char* name, size_t capacity, uint8_t* hash ))
{
    if (name == NULL || capacity == 0 || g.lock == NULL)
        return -EINVAL;
    int ret = -ENOENT;
    dmosi_mutex_lock(g.lock);
    ensure_channels_loaded_locked();
    if (index < LIBMESHCORE_MAX_CHANNELS && g.channels[index].used)
    {
        Dmod_SnPrintf(name, capacity, "%s", g.channels[index].name);
        if (hash != NULL)
            *hash = g.channels[index].channel.hash;
        ret = 0;
    }
    dmosi_mutex_unlock(g.lock);
    return ret;
}

static uint32_t now_seconds(void)
{
    dmutc_time_t utc;
    if (dmutc_get(&utc) == 0 && utc.seconds > 0)
        return (uint32_t)utc.seconds;
    return Dmod_GetUptime() / 1000u;                        /* no time: the uptime still grows, as the receivers of a mesh want */
}

dmod_libmeshcore_api_declaration(1.0, int, _send_text, ( uint8_t channel_index, const char* text ))
{
    if (text == NULL || text[0] == '\0' || g.lock == NULL)
        return -EINVAL;
    dmosi_mutex_lock(g.lock);
    if (!g.started)
    {
        dmosi_mutex_unlock(g.lock);
        return -ENODEV;
    }
    if (channel_index >= LIBMESHCORE_MAX_CHANNELS || !g.channels[channel_index].used)
    {
        dmosi_mutex_unlock(g.lock);
        return -ENOENT;
    }
    channel_slot_t channel = g.channels[channel_index];
    char path[PATH_MAX_LENGTH + 1];
    char name[LIBMESHCORE_NODE_NAME_MAX + 1];
    strcpy(path, g.path);
    strcpy(name, g.settings.name);
    dmosi_mutex_unlock(g.lock);

    libmeshcore_group_text_t group;
    memset(&group, 0, sizeof(group));
    group.timestamp = now_seconds();
    Dmod_SnPrintf(group.text, sizeof(group.text), "%s: %s", name, text);
    libmeshcore_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.route = LIBMESHCORE_ROUTE_FLOOD;
    packet.hash_size = 1;
    int ret = libmeshcore_group_text_encode(&channel.channel, &group, &packet);
    if (ret != 0)
        return ret;
    dmdrvi_lora_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    size_t length = 0;
    ret = libmeshcore_packet_encode(&packet, frame.data, sizeof(frame.data), &length);
    if (ret != 0)
        return ret;
    frame.length = (uint16_t)length;
    frame.timeout_ms = 5000;

    void* file = Dmod_FileOpen(path, "r+");
    if (file == NULL)
        return -ENODEV;
    ret = -EBUSY;
    for (unsigned attempt = 0; attempt < SEND_TRIES && ret == -EBUSY; attempt++)
    {
        dmdrvi_lora_cad_t cad = { .timeout_ms = 300 };
        int looked = Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_CAD, &cad);
        if (looked == 0 && cad.busy)
        {
            dmosi_thread_sleep(150u + 100u * attempt + (Dmod_GetUptime() & 0x7Fu));      /* somebody sends: wait for a different time each try */
            continue;
        }
        ret = Dmod_Ioctl(file, DMDRVI_IOCTL_LORA_SEND, &frame);
    }
    Dmod_FileClose(file);
    if (ret == 0)
    {
        dmosi_mutex_lock(g.lock);
        note_message(&channel, &group, &packet, 0, Dmod_GetUptime(), true);
        g.sent++;
        changed();
        dmosi_mutex_unlock(g.lock);
    }
    return ret;
}

/* ---- The module ---- */

int dmod_init( const Dmod_Config_t* Config )
{
    (void)Config;
    memset(&g, 0, sizeof(g));
    g.lock = dmosi_mutex_create(false);
    if (g.lock == NULL)
        return -ENOMEM;
    libmeshcore_settings_defaults(&g.settings);
    /* the public channel of MeshCore is always there */
    strcpy(g.channels[0].name, "Public");
    (void)libmeshcore_channel_from_text(&g.channels[0].channel, "izOH6cXN6mrJ5e26oRXNcg==");
    g.channels[0].used = true;
    g.worker = dmosi_thread_create(worker_main, NULL, 2, 6144, "meshcore", dmosi_process_current());
    if (g.worker == NULL)
    {
        dmosi_mutex_destroy(g.lock);
        g.lock = NULL;
        return -ENOMEM;
    }
    return 0;
}

int dmod_deinit( void )
{
    if (g.started)
        return -EBUSY;
    g.stopping = true;
    if (g.worker != NULL)
    {
        (void)dmosi_thread_join(g.worker);
        dmosi_thread_destroy(g.worker);
        g.worker = NULL;
    }
    dmosi_mutex_destroy(g.lock);
    g.lock = NULL;
    return 0;
}
