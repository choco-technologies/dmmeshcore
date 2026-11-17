#include "dmod.h"
#include "internal.h"
#include <errno.h>
#include <string.h>

/* The header of the file: "MCH1" and the size of a record (a file of another size is not read: a new version of the record starts a new file) */
#define HISTORY_MAGIC   0x3148434Du

typedef struct
{
    uint32_t    magic;
    uint32_t    record_size;
} header_t;

static bool header_ok(const header_t* header)
{
    return header->magic == HISTORY_MAGIC && header->record_size == sizeof(libmeshcore_record_t);
}

int libmeshcore_history_read(const char* path, libmeshcore_record_t* records, size_t capacity, size_t* count, size_t* in_file, bool* tidy)
{
    if (path == NULL || records == NULL || capacity == 0u || count == NULL)
        return -EINVAL;
    *count = 0;
    if (in_file != NULL)
        *in_file = 0;
    if (tidy != NULL)
        *tidy = true;
    void* file = Dmod_FileOpen(path, "rb");
    if (file == NULL)
        return -ENOENT;
    header_t header;
    if (Dmod_FileRead(&header, sizeof(header), 1, file) != 1 || !header_ok(&header))
    {
        Dmod_FileClose(file);
        return -ENOENT;
    }
    /* The records are read one after the other into a ring of `capacity`: the last ones stay */
    size_t total = 0;
    libmeshcore_record_t record;
    while (Dmod_FileRead(&record, sizeof(record), 1, file) == 1)
    {
        record.channel[sizeof(record.channel) - 1U] = '\0';
        record.text[sizeof(record.text) - 1U] = '\0';
        records[total % capacity] = record;
        total++;
    }
    if (tidy != NULL)
        *tidy = Dmod_FileSize(file) == (Dmod_FileSize_t)(sizeof(header) + total * sizeof(record));
    Dmod_FileClose(file);
    size_t kept = total < capacity ? total : capacity;
    if (total > capacity)
    {
        /* rotate the ring so that the oldest is first */
        size_t first = total % capacity;
        libmeshcore_record_t* tmp = Dmod_Malloc(sizeof(libmeshcore_record_t) * capacity);
        if (tmp == NULL)
            return -ENOMEM;
        for (size_t i = 0; i < capacity; i++)
            tmp[i] = records[(first + i) % capacity];
        memcpy(records, tmp, sizeof(libmeshcore_record_t) * capacity);
        Dmod_Free(tmp);
    }
    *count = kept;
    if (in_file != NULL)
        *in_file = total;
    return 0;
}

int libmeshcore_history_append(const char* path, const libmeshcore_record_t* record)
{
    if (path == NULL || record == NULL)
        return -EINVAL;
    void* file = Dmod_FileOpen(path, "rb");
    bool fresh = file == NULL;
    if (!fresh)
    {
        header_t header;
        fresh = Dmod_FileRead(&header, sizeof(header), 1, file) != 1 || !header_ok(&header);
        Dmod_FileClose(file);
    }
    if (fresh)
        return libmeshcore_history_write(path, record, 1);
    file = Dmod_FileOpen(path, "ab");
    if (file == NULL)
        return -EIO;
    size_t written = Dmod_FileWrite(record, sizeof(*record), 1, file);
    Dmod_FileClose(file);
    return written == 1 ? 0 : -EIO;
}

int libmeshcore_history_write(const char* path, const libmeshcore_record_t* records, size_t count)
{
    if (path == NULL || (records == NULL && count != 0u))
        return -EINVAL;
    void* file = Dmod_FileOpen(path, "wb");
    if (file == NULL)
        return -EIO;
    header_t header = { HISTORY_MAGIC, (uint32_t)sizeof(libmeshcore_record_t) };
    bool ok = Dmod_FileWrite(&header, sizeof(header), 1, file) == 1;
    for (size_t i = 0; ok && i < count; i++)
        ok = Dmod_FileWrite(&records[i], sizeof(records[i]), 1, file) == 1;
    Dmod_FileClose(file);
    return ok ? 0 : -EIO;
}
