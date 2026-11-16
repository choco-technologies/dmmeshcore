#include "dmod.h"
#include "internal.h"
#include "dmini.h"
#include <errno.h>
#include <string.h>

/* The presets: the first three are the ones of the apps of MeshCore (the first is verified on the air: it is what the mesh around uses) */
static void preset_numbers(uint8_t index, libmeshcore_preset_info_t* p)
{
    memset(p, 0, sizeof(*p));
    switch (index)
    {
        case LIBMESHCORE_PRESET_EU_NARROW:
            Dmod_SnPrintf(p->name, sizeof(p->name), "EU/UK narrow");
            p->frequency_hz = 869618000u; p->bandwidth_hz = 62500u; p->spreading_factor = 8; p->coding_rate = 8;
            break;
        case LIBMESHCORE_PRESET_EU_LONG:
            Dmod_SnPrintf(p->name, sizeof(p->name), "EU/UK long range");
            p->frequency_hz = 869525000u; p->bandwidth_hz = 250000u; p->spreading_factor = 11; p->coding_rate = 5;
            break;
        case LIBMESHCORE_PRESET_US:
            Dmod_SnPrintf(p->name, sizeof(p->name), "USA/Canada");
            p->frequency_hz = 910525000u; p->bandwidth_hz = 62500u; p->spreading_factor = 7; p->coding_rate = 5;
            break;
        default:
            Dmod_SnPrintf(p->name, sizeof(p->name), "Custom");
            break;
    }
}

dmod_libmeshcore_api_declaration(1.0, int, _preset, ( uint8_t index, libmeshcore_preset_info_t* preset ))
{
    if (preset == NULL || index >= LIBMESHCORE_PRESET_COUNT)
        return -EINVAL;
    preset_numbers(index, preset);
    return 0;
}

void libmeshcore_settings_defaults(libmeshcore_settings_t* s)
{
    libmeshcore_preset_info_t p;
    memset(s, 0, sizeof(*s));
    preset_numbers(LIBMESHCORE_PRESET_EU_NARROW, &p);
    s->enabled = true;
    s->preset = LIBMESHCORE_PRESET_EU_NARROW;
    s->frequency_hz = p.frequency_hz;
    s->bandwidth_hz = p.bandwidth_hz;
    s->spreading_factor = p.spreading_factor;
    s->coding_rate = p.coding_rate;
    s->tx_power_dbm = 14;
    s->path_hash_size = 1;
    Dmod_SnPrintf(s->name, sizeof(s->name), "DMOD node");
}

int libmeshcore_settings_check(libmeshcore_settings_t* s)
{
    if (s->preset >= LIBMESHCORE_PRESET_COUNT)
        return -EINVAL;
    if (s->preset != LIBMESHCORE_PRESET_CUSTOM)
    {
        libmeshcore_preset_info_t p;
        preset_numbers(s->preset, &p);
        s->frequency_hz = p.frequency_hz;
        s->bandwidth_hz = p.bandwidth_hz;
        s->spreading_factor = p.spreading_factor;
        s->coding_rate = p.coding_rate;
    }
    s->name[LIBMESHCORE_NODE_NAME_MAX] = '\0';
    /* (the limits of the SX126x: the radio refuses the rest anyway, this says it before it is kept) */
    if (s->name[0] == '\0' || s->frequency_hz < 150000000u || s->frequency_hz > 960000000u || s->bandwidth_hz < 7800u || s->bandwidth_hz > 500000u ||
        s->spreading_factor < 5 || s->spreading_factor > 12 || s->coding_rate < 5 || s->coding_rate > 8 || s->tx_power_dbm < -9 || s->tx_power_dbm > 22 ||
        s->path_hash_size < 1 || s->path_hash_size > 3)
        return -EINVAL;
    return 0;
}

int libmeshcore_settings_load(libmeshcore_settings_t* s)
{
    libmeshcore_settings_defaults(s);
    dmini_context_t ini = dmini_create();
    if (ini == NULL)
        return -ENOMEM;
    if (dmini_parse_file(ini, LIBMESHCORE_SETTINGS_FILE) != 0 || !dmini_has_section(ini, "meshcore"))
    {
        dmini_destroy(ini);
        return 0;                                       /* no file: the defaults */
    }
    libmeshcore_settings_t t = *s;
    t.enabled = dmini_get_int(ini, "meshcore", "enabled", 1) != 0;
    t.preset = (uint8_t)dmini_get_int(ini, "meshcore", "preset", t.preset);
    t.frequency_hz = (uint32_t)dmini_get_int(ini, "meshcore", "frequency_hz", (int)t.frequency_hz);
    t.bandwidth_hz = (uint32_t)dmini_get_int(ini, "meshcore", "bandwidth_hz", (int)t.bandwidth_hz);
    t.spreading_factor = (uint8_t)dmini_get_int(ini, "meshcore", "spreading_factor", t.spreading_factor);
    t.coding_rate = (uint8_t)dmini_get_int(ini, "meshcore", "coding_rate", t.coding_rate);
    t.tx_power_dbm = (int8_t)dmini_get_int(ini, "meshcore", "tx_power_dbm", t.tx_power_dbm);
    t.path_hash_size = (uint8_t)dmini_get_int(ini, "meshcore", "path_hash_size", t.path_hash_size);
    const char* name = dmini_get_string(ini, "meshcore", "name", NULL);
    if (name != NULL && name[0] != '\0')
    {
        Dmod_SnPrintf(t.name, sizeof(t.name), "%s", name);
    }
    dmini_destroy(ini);
    if (libmeshcore_settings_check(&t) == 0)
        *s = t;                                         /* a file that does not hold is not used: the defaults stay */
    return 0;
}

int libmeshcore_settings_save(const libmeshcore_settings_t* s)
{
    dmini_context_t ini = dmini_create();
    if (ini == NULL)
        return -ENOMEM;
    dmini_set_int(ini, "meshcore", "enabled", s->enabled ? 1 : 0);
    dmini_set_int(ini, "meshcore", "preset", s->preset);
    dmini_set_int(ini, "meshcore", "frequency_hz", (int)s->frequency_hz);
    dmini_set_int(ini, "meshcore", "bandwidth_hz", (int)s->bandwidth_hz);
    dmini_set_int(ini, "meshcore", "spreading_factor", s->spreading_factor);
    dmini_set_int(ini, "meshcore", "coding_rate", s->coding_rate);
    dmini_set_int(ini, "meshcore", "tx_power_dbm", s->tx_power_dbm);
    dmini_set_int(ini, "meshcore", "path_hash_size", s->path_hash_size);
    dmini_set_string(ini, "meshcore", "name", s->name);
    int ret = dmini_generate_file(ini, LIBMESHCORE_SETTINGS_FILE);
    dmini_destroy(ini);
    return ret == 0 ? 0 : -EIO;
}
