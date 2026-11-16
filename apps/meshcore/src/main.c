#include "dmod.h"
#include "dmosi.h"
#include "libdmview.h"
#include "libmeshcore.h"
#include "format.h"
#include <errno.h>
#include <string.h>

/**
 * @brief meshcore - the screen of the MeshCore mesh: the state of the radio, the nodes that were heard, the texts of the channels.
 *
 * Usage: meshcore [--display NAME] [<view>]
 *
 * NAME is the display it takes, or the rent of one that its owner gave it (the launcher of phoneui passes it to every application it starts, so the
 * application is shown in a part of the screen); without it, the first display. The view is views/meshcore.html converted by dmod at build time.
 * Everything that is the mesh is in libmeshcore (its service listens on the radio since libsystemd starts it for the LoRa node): this file only
 * shows what the library says and passes what the user does to it. It ends when its claim is gone - the owner took the rent back.
 */

#define DEFAULT_VIEW    "/eviews/meshcore/meshcore.dmv"     /* dmod-boot: the views of the flash modules */
#define WAIT_MS         100u
#define DISPLAY_WAIT_MS 10000u
#define EVENT_WAIT_MS   500u
#define NODE_ROWS       5u
#define TEXT_ROWS       4u
#define LINE_WIDTH      30u

#define COMPOSE_MAX     100u        /* the bytes of a text to send (the frame takes 184 bytes with the name of this node, the time and the padding of AES) */
#define KEY_ENTER       0x28
#define KEY_BACKSPACE   0x2A

enum { PAGE_MAIN = 0, PAGE_NODES = 1, PAGE_TEXTS = 2, PAGE_COMPOSE = 3 };

typedef struct
{
    uint32_t    claim;
    int         page;
    size_t      first;          /* the first row of the list that is shown */
    uint32_t    serial;
    uint32_t    shown_at;       /* when the page was drawn */
    uint8_t     channel;        /* the channel a text is written to */
    char        text[COMPOSE_MAX + 1];
    char        status[40];     /* the line under the text: what happened to the last one */
    int         utc_offset_min;
    bool        eu_dst;
} ui_t;

static void set_int(const ui_t* ui, const char* name, int32_t value)
{
    (void)libdmview_prop_set_int(ui->claim, name, value);
}

static void set_text(const ui_t* ui, const char* name, const char* value)
{
    (void)libdmview_prop_set_string(ui->claim, name, value);
}

static void row_name(char* out, size_t size, const char* prefix, unsigned index, const char* field)
{
    Dmod_SnPrintf(out, size, "%s%u.%s", prefix, index, field);
}

static void show_main(const ui_t* ui)
{
    libmeshcore_status_t s;
    libmeshcore_settings_t cfg;
    libmeshcore_preset_info_t preset;
    char line[48];
    (void)libmeshcore_status(&s);
    (void)libmeshcore_settings_get(&cfg);
    (void)libmeshcore_preset(cfg.preset, &preset);
    set_text(ui, "st.title", !s.running ? "No radio" : !cfg.enabled ? "Off" : s.listening ? "Listening" : "Starting...");
    Dmod_SnPrintf(line, sizeof(line), "%u.%03u MHz, SF%u, %s", (unsigned)(cfg.frequency_hz / 1000000u), (unsigned)(cfg.frequency_hz / 1000u % 1000u),
                  (unsigned)cfg.spreading_factor, preset.name);
    set_text(ui, "st.line1", line);
    libmeshcore_contact_t contacts[LIBMESHCORE_MAX_CONTACTS];
    size_t nodes = 0;
    (void)libmeshcore_contacts(contacts, LIBMESHCORE_MAX_CONTACTS, &nodes);
    Dmod_SnPrintf(line, sizeof(line), "%u packets, %u nodes, %u texts", (unsigned)s.packets, (unsigned)nodes, (unsigned)s.texts);
    set_text(ui, "st.line2", line);
    if (s.packets != 0u)
        Dmod_SnPrintf(line, sizeof(line), "Last: %d dBm", (int)s.last_rssi_dbm);
    else
        Dmod_SnPrintf(line, sizeof(line), "Nothing heard yet");
    set_text(ui, "st.line3", line);
}

static void show_nodes(const ui_t* ui)
{
    libmeshcore_contact_t contacts[LIBMESHCORE_MAX_CONTACTS];
    size_t total = 0;
    char name[24], text[64], info[32];
    (void)libmeshcore_contacts(contacts, LIBMESHCORE_MAX_CONTACTS, &total);
    size_t shown = 0;
    for (unsigned i = 0; i < NODE_ROWS; i++)
    {
        size_t index = ui->first + i;
        bool on = index < total;
        row_name(name, sizeof(name), "n", i, "on");
        set_int(ui, name, on ? 1 : 0);
        if (!on)
            continue;
        shown++;
        row_name(name, sizeof(name), "n", i, "name");
        set_text(ui, name, contacts[index].name[0] != '\0' ? contacts[index].name : "(no name)");
        meshcore_node_line(&contacts[index], text, sizeof(text));
        row_name(name, sizeof(name), "n", i, "sub");
        set_text(ui, name, text);
    }
    meshcore_range(ui->first, shown, total, info, sizeof(info));
    set_text(ui, "nodes.info", info);
}

static void show_texts(const ui_t* ui)
{
    libmeshcore_message_t messages[LIBMESHCORE_MAX_MESSAGES];
    size_t total = 0;
    char name[24], l1[LINE_WIDTH + 4], l2[LINE_WIDTH + 4], who[56], info[32];
    (void)libmeshcore_messages(messages, LIBMESHCORE_MAX_MESSAGES, &total);
    size_t shown = 0;
    for (unsigned i = 0; i < TEXT_ROWS; i++)
    {
        size_t index = ui->first + i;
        bool on = index < total;
        row_name(name, sizeof(name), "t", i, "on");
        set_int(ui, name, on ? 1 : 0);
        if (!on)
            continue;
        shown++;
        meshcore_who(&messages[index], ui->utc_offset_min, ui->eu_dst, who, sizeof(who));
        row_name(name, sizeof(name), "t", i, "who");
        set_text(ui, name, who);
        meshcore_wrap(messages[index].text, LINE_WIDTH, l1, l2, sizeof(l1));
        row_name(name, sizeof(name), "t", i, "l1");
        set_text(ui, name, l1);
        row_name(name, sizeof(name), "t", i, "l2");
        set_text(ui, name, l2);
    }
    meshcore_range(ui->first, shown, total, info, sizeof(info));
    set_text(ui, "texts.info", info);
}

static void show_compose(const ui_t* ui)
{
    char name[40], text[COMPOSE_MAX + 8];
    if (libmeshcore_channel_name(ui->channel, name, sizeof(name), NULL) != 0)
        Dmod_SnPrintf(name, sizeof(name), "?");
    Dmod_SnPrintf(text, sizeof(text), "To: %s", name);
    set_text(ui, "cp.to", text);
    Dmod_SnPrintf(text, sizeof(text), "%s|", ui->text);
    set_text(ui, "cp.text", text);
    set_text(ui, "cp.status", ui->status);
}

static void show(ui_t* ui)
{
    set_int(ui, "page", ui->page);
    if (ui->page == PAGE_COMPOSE)
        show_compose(ui);
    else if (ui->page == PAGE_NODES)
        show_nodes(ui);
    else if (ui->page == PAGE_TEXTS)
        show_texts(ui);
    else
        show_main(ui);
}

static void open_compose(ui_t* ui, const char* prefix)
{
    ui->page = PAGE_COMPOSE;
    ui->channel = 0;
    Dmod_SnPrintf(ui->text, sizeof(ui->text), "%s", prefix);
    ui->status[0] = '\0';
    show(ui);
}

/* A tap on a text: write to its channel, to its sender */
static void reply_to(ui_t* ui, size_t index)
{
    libmeshcore_message_t messages[LIBMESHCORE_MAX_MESSAGES];
    size_t total = 0;
    char prefix[48];
    (void)libmeshcore_messages(messages, LIBMESHCORE_MAX_MESSAGES, &total);
    if (index >= total)
        return;
    meshcore_reply_prefix(messages[index].text, prefix, sizeof(prefix));
    open_compose(ui, prefix);
    for (uint8_t i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
    {
        char name[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
        if (libmeshcore_channel_name(i, name, sizeof(name), NULL) == 0 && strcmp(name, messages[index].channel) == 0)
        {
            ui->channel = i;
            break;
        }
    }
    show(ui);
}

static void send_text(ui_t* ui)
{
    if (ui->text[0] == '\0')
        return;
    Dmod_SnPrintf(ui->status, sizeof(ui->status), "Sending...");
    show(ui);                                                       /* (a send looks at the channel and takes seconds: the page says so first) */
    int ret = libmeshcore_send_text(ui->channel, ui->text);
    if (ret == 0)
    {
        ui->text[0] = '\0';
        Dmod_SnPrintf(ui->status, sizeof(ui->status), "Sent");
    }
    else
        Dmod_SnPrintf(ui->status, sizeof(ui->status), ret == -ENODEV ? "No radio" : ret == -EBUSY ? "The channel is busy, try again" : "Could not send (%d)", ret);
    show(ui);
}

static void handle_compose(ui_t* ui, const libdmview_event_t* ev)
{
    if (strcmp(ev->name, "char") == 0)
        (void)meshcore_append(ui->text, sizeof(ui->text), (uint32_t)ev->value);
    else if (strcmp(ev->name, "key") == 0 && ev->value == KEY_BACKSPACE)
        meshcore_backspace(ui->text);
    else if ((strcmp(ev->name, "key") == 0 && ev->value == KEY_ENTER) || strcmp(ev->name, "send") == 0)
    {
        send_text(ui);
        return;
    }
    else if (strcmp(ev->name, "back") == 0)
    {
        ui->page = PAGE_TEXTS;
        ui->first = 0;
    }
    else
        return;
    ui->status[0] = '\0';
    show(ui);
}

static void handle(ui_t* ui, const libdmview_event_t* ev)
{
    size_t rows = ui->page == PAGE_NODES ? NODE_ROWS : TEXT_ROWS;
    if (ui->page == PAGE_COMPOSE)
    {
        handle_compose(ui, ev);
        return;
    }
    if (strcmp(ev->name, "new") == 0 && ui->page == PAGE_TEXTS)
    {
        open_compose(ui, "");
        return;
    }
    if (strcmp(ev->name, "row") == 0 && ui->page == PAGE_TEXTS)
    {
        reply_to(ui, ui->first + (size_t)ev->value);
        return;
    }
    if (strcmp(ev->name, "open.nodes") == 0)
    {
        ui->page = PAGE_NODES;
        ui->first = 0;
    }
    else if (strcmp(ev->name, "open.texts") == 0)
    {
        ui->page = PAGE_TEXTS;
        ui->first = 0;
    }
    else if (strcmp(ev->name, "back") == 0)
        ui->page = PAGE_MAIN;
    else if (strcmp(ev->name, "next") == 0)
        ui->first += rows;
    else if (strcmp(ev->name, "prev") == 0)
        ui->first = ui->first >= rows ? ui->first - rows : 0;
    else
        return;
    if (ui->page != PAGE_MAIN)
    {
        /* a page past the end is the last one */
        size_t total = 0;
        if (ui->page == PAGE_NODES)
        {
            libmeshcore_contact_t c[LIBMESHCORE_MAX_CONTACTS];
            (void)libmeshcore_contacts(c, LIBMESHCORE_MAX_CONTACTS, &total);
        }
        else
        {
            libmeshcore_message_t m[LIBMESHCORE_MAX_MESSAGES];
            (void)libmeshcore_messages(m, LIBMESHCORE_MAX_MESSAGES, &total);
        }
        if (ui->first >= total && total > 0)
            ui->first = (total - 1u) / rows * rows;
        else if (total == 0)
            ui->first = 0;
    }
    show(ui);
}

/* The rent or the display may be there a moment after this starts */
static int claim_display(const char* display, const char* view, uint32_t* claim)
{
    dmosi_semaphore_t wakeup = dmosi_semaphore_create(0, 1);
    if (wakeup == NULL)
        return -ENOMEM;
    int ret = -ENODEV;
    for (uint32_t waited = 0; waited <= DISPLAY_WAIT_MS; waited += WAIT_MS)
    {
        ret = libdmview_claim(display, view, claim);
        if (ret != -ENODEV)
            break;
        (void)dmosi_semaphore_wait(wakeup, 1, (int32_t)WAIT_MS);
    }
    dmosi_semaphore_destroy(wakeup);
    return ret;
}

int main(int argc, char* argv[])
{
    const char* display = NULL;
    const char* view = DEFAULT_VIEW;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--display") == 0 && i + 1 < argc)
            display = argv[++i];
        else if (argv[i][0] != '\0')
            view = argv[i];
    }

    ui_t ui;
    memset(&ui, 0, sizeof(ui));
    /* the local time: the same variables as the status bar of the phone, so that the times agree (60 = CET; the summer time of the EU unless PHONEUI_DST says off) */
    const char* offset = Dmod_GetEnv("PHONEUI_UTC_OFFSET");
    const char* dst = Dmod_GetEnv("PHONEUI_DST");
    ui.utc_offset_min = 60;
    if (offset != NULL && offset[0] != '\0')
    {
        bool negative = offset[0] == '-';
        int value = 0;
        for (const char* c = offset + (negative ? 1 : 0); *c >= '0' && *c <= '9'; c++)
            value = value * 10 + (*c - '0');
        ui.utc_offset_min = negative ? -value : value;
    }
    ui.eu_dst = dst == NULL || dst[0] == '\0' || dst[0] == 'e' || dst[0] == 'E';
    int ret = claim_display(display, view, &ui.claim);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("meshcore: cannot claim %s (%d)\n", (display != NULL) ? display : "a display", ret);
        return ret;
    }
    show(&ui);
    libdmview_event_t ev;
    while ((ret = libdmview_wait_event(ui.claim, &ev, (int32_t)EVENT_WAIT_MS)) != -ENOENT)
    {
        if (ret == 0)
            handle(&ui, &ev);
        /* what the service learned meanwhile */
        libmeshcore_status_t s;
        (void)libmeshcore_status(&s);
        if (s.serial != ui.serial)
        {
            ui.serial = s.serial;
            show(&ui);
        }
    }
    return 0;
}
