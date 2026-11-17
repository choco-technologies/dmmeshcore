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

/* history.c: the texts that were heard or sent, kept in a file, so that a reboot (or a crash) does not take them away. The file is a header and records of
 * a fixed size, the newest at the end: a text is appended, and when the file holds much more than the service remembers it is written anew. */
typedef struct
{
    uint32_t    timestamp;                              /* by the clock of the sender */
    uint32_t    heard_utc;                              /* by the clock of this node, 0: it was not set */
    int16_t     rssi_dbm;
    uint8_t     hops;
    uint8_t     mine;
    char        channel[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
    char        text[LIBMESHCORE_MESSAGE_TEXT_MAX + 1];
} libmeshcore_record_t;

/* The last `capacity` records of the file, the oldest first, into `records`; `*in_file` gets how many records the file holds. -ENOENT: no such file (or not a
 * file of this kind), the others: the error of the file system. A record that is cut short at the end (the power went off while it was written) is dropped,
 * and `*tidy` says false: a record appended to such a file would not be where it should be, so the file has to be written anew. */
int libmeshcore_history_read(const char* path, libmeshcore_record_t* records, size_t capacity, size_t* count, size_t* in_file, bool* tidy);
/* One record at the end of the file (the header first, when there is no file yet). */
int libmeshcore_history_append(const char* path, const libmeshcore_record_t* record);
/* The file anew, made of these records (the oldest first). */
int libmeshcore_history_write(const char* path, const libmeshcore_record_t* records, size_t count);

#define LIBMESHCORE_HISTORY_FILE    "/ext/meshcore-messages.bin"
/* The file may hold this many records before it is written anew (4 times what the service remembers: a write of the whole file is rare, and the flash is spared) */
#define LIBMESHCORE_HISTORY_LIMIT   (4U * LIBMESHCORE_MAX_MESSAGES)

#define LIBMESHCORE_SETTINGS_FILE   "/ext/meshcore.ini"
#define LIBMESHCORE_CHANNELS_FILE   "/ext/meshcore-channels.ini"

#endif
