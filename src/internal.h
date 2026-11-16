#ifndef LIBMESHCORE_INTERNAL_H
#define LIBMESHCORE_INTERNAL_H

#include "libmeshcore.h"

/* settings.c: the presets, the checks, the file */
void libmeshcore_settings_defaults(libmeshcore_settings_t* settings);
int  libmeshcore_settings_check(libmeshcore_settings_t* settings);          /* fills the numbers of a preset; 0 or -EINVAL */
int  libmeshcore_settings_load(libmeshcore_settings_t* settings);           /* the file, or the defaults when there is none */
int  libmeshcore_settings_save(const libmeshcore_settings_t* settings);

/* service.c: the start and the stop of the unit of a radio (the DIF of libsystemd is in packet.c, the one file of the module with the registrations) */
int libmeshcore_service_begin(const char* unit, int argc, const char* const argv[]);
int libmeshcore_service_end(const char* unit);

#define LIBMESHCORE_SETTINGS_FILE   "/ext/meshcore.ini"

#endif
