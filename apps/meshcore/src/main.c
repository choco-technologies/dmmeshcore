#include "dmod.h"
#include "dmosi.h"
#include "libdmview.h"
#include "libmeshcore.h"
#include "format.h"
#include <errno.h>
#include <string.h>

/**
 * @brief meshcore - the screen of the MeshCore mesh: the state of the radio, the nodes that were heard, the channels with their texts in bubbles.
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
#define LIST_ROWS       5u
#define CHAT_ROWS       12u         /* the lines of the chat that the page has (of 16 px) */
#define SCROLL_STEP     4u
#define BUBBLE_WIDTH    25u         /* the bytes of a line of a bubble */
#define BUBBLE_LINES    6u
#define MAX_ROWS        (LIBMESHCORE_MAX_MESSAGES * (BUBBLE_LINES + 1u))
#define ROW_TEXT        48u
#define LINE_WIDTH      30u

#define COMPOSE_MAX     100u        /* the bytes of a text to send (the frame takes 184 bytes with the name of this node, the time and the padding of AES) */
#define INPUT_WIDTH     20u         /* the bytes of a line of the field under the chat (it shows two lines) */
#define FULL_WIDTH      24u         /* the bytes of a line of the full-screen field (it shows six) */
#define FULL_LINES      6u
#define EDIT_LINES      8u          /* the lines of a text of COMPOSE_MAX bytes, at most */
#define LINK_SECRET_MAX 64u
#define NAME_TYPED_MAX  (LIBMESHCORE_CHANNEL_NAME_MAX + 8u)
#define KEY_TYPED_MAX   48u
#define FIELD_SHOWN     26u         /* the bytes of a single-line field that are shown (its end) */
#define KEY_ENTER       0x28
#define KEY_BACKSPACE   0x2A
#define KEY_DOWN        0x51
#define KEY_UP          0x52
#define KEY_RIGHT       0x4F
#define KEY_LEFT        0x50
#define KEY_HOME        0x4A
#define KEY_END         0x4D

enum { PAGE_MAIN = 0, PAGE_NODES = 1, PAGE_CHANNELS = 2, PAGE_CHAT = 3, PAGE_COMPOSE = 4, PAGE_ADD = 5 };

/* A line of the chat: which part of a bubble (0 the head, 1 the middle, 2 the last line), whose, and the message it is of (to reply to) */
typedef struct
{
    uint8_t     part;
    bool        mine;
    int16_t     message;
    char        text[ROW_TEXT];
} chat_row_t;

typedef struct
{
    uint32_t    claim;
    int         page;
    size_t      first;          /* the first row of the list that is shown */
    size_t      scroll;         /* the lines of the chat that are under the window (0: the newest text is at the bottom) */
    uint32_t    serial;
    uint8_t     channel;        /* the channel of the chat, the one a text is written to */
    char        text[COMPOSE_MAX + 1];
    size_t      cursor;         /* where in the text the keys write (a byte of it) */
    size_t      chat_win;       /* the first of the two lines of the text that the field under the chat shows */
    size_t      full_win;       /* the first of the lines of the full-screen field */
    char        name[NAME_TYPED_MAX + 1];       /* the page of a new channel: the two fields */
    char        key[KEY_TYPED_MAX + 1];
    size_t      name_cursor, key_cursor;
    int         field;          /* of the two: 0 the name, 1 the key */
    char        status[40];     /* what happened to the last text (the field under the chat shows it while it is empty) */
    int         utc_offset_min;
    bool        eu_dst;
    uint8_t     listed[LIBMESHCORE_MAX_CHANNELS];       /* the channel of every row of the list of channels */
    size_t      listed_count;
    int16_t     row_message[CHAT_ROWS];                 /* the message of every line of the chat that is shown (-1: none) */
    libmeshcore_message_t* messages;                    /* (not on the stack: 32 of them) */
    chat_row_t* rows;
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

/* ---- The channels ---- */

static size_t read_messages(ui_t* ui)
{
    size_t total = 0;
    (void)libmeshcore_messages(ui->messages, LIBMESHCORE_MAX_MESSAGES, &total);
    return total;
}

static void list_channels(ui_t* ui)
{
    ui->listed_count = 0;
    for (uint8_t i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
    {
        char name[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
        if (libmeshcore_channel_name(i, name, sizeof(name), NULL) == 0)
            ui->listed[ui->listed_count++] = i;
    }
}

static void show_channels(ui_t* ui)
{
    char name[24], sub[LINE_WIDTH + 8], spare[LINE_WIDTH + 8], info[32];
    list_channels(ui);
    size_t total = read_messages(ui);
    size_t shown = 0;
    for (unsigned i = 0; i < LIST_ROWS; i++)
    {
        size_t index = ui->first + i;
        bool on = index < ui->listed_count;
        row_name(name, sizeof(name), "c", i, "on");
        set_int(ui, name, on ? 1 : 0);
        if (!on)
            continue;
        shown++;
        char channel[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
        (void)libmeshcore_channel_name(ui->listed[index], channel, sizeof(channel), NULL);
        row_name(name, sizeof(name), "c", i, "name");
        set_text(ui, name, channel);
        Dmod_SnPrintf(sub, sizeof(sub), "No texts yet");
        for (size_t m = 0; m < total; m++)                                  /* (the newest first) */
        {
            if (strcmp(ui->messages[m].channel, channel) == 0)
            {
                meshcore_wrap(ui->messages[m].text, LINE_WIDTH, sub, spare, sizeof(sub));
                break;
            }
        }
        row_name(name, sizeof(name), "c", i, "sub");
        set_text(ui, name, sub);
    }
    meshcore_range(ui->first, shown, ui->listed_count, info, sizeof(info));
    set_text(ui, "chats.info", info);
}

/* ---- The chat ---- */

static void add_row(ui_t* ui, size_t* count, uint8_t part, bool mine, int16_t message, const char* text)
{
    if (*count >= MAX_ROWS)
        return;
    chat_row_t* row = &ui->rows[(*count)++];
    row->part = part;
    row->mine = mine;
    row->message = message;
    Dmod_SnPrintf(row->text, sizeof(row->text), "%s", text);
}

/* The lines of all the texts of the channel, the oldest first: a head (who, when, hops) and the lines of the text of every bubble */
static size_t build_rows(ui_t* ui)
{
    char channel[LIBMESHCORE_CHANNEL_NAME_MAX + 1], sender[LIBMESHCORE_NODE_NAME_MAX + 2], head[ROW_TEXT], lines[BUBBLE_LINES][ROW_TEXT];
    size_t total = read_messages(ui), count = 0;
    if (libmeshcore_channel_name(ui->channel, channel, sizeof(channel), NULL) != 0)
        return 0;
    for (size_t m = total; m > 0; m--)
    {
        const libmeshcore_message_t* message = &ui->messages[m - 1u];
        const char* body;
        if (strcmp(message->channel, channel) != 0)
            continue;
        meshcore_split_sender(message->text, sender, sizeof(sender), &body);
        size_t n;
        char link_name[LIBMESHCORE_CHANNEL_NAME_MAX + 1], link_secret[LINK_SECRET_MAX];
        uint8_t known;
        if (libmeshcore_channel_link_parse(body, link_name, sizeof(link_name), link_secret, sizeof(link_secret)) == 0)
        {
            /* a link that adds a channel: what it is and what a tap does (the link itself is a line of a code nobody reads) */
            Dmod_SnPrintf(lines[0], ROW_TEXT, "Channel %s", link_name);
            Dmod_SnPrintf(lines[1], ROW_TEXT, libmeshcore_channel_find(link_secret, &known) == 0 ? "Tap to open it" : "Tap to add it");
            n = 2u;
        }
        else
        {
            n = meshcore_wrap_lines(body, BUBBLE_WIDTH, &lines[0][0], ROW_TEXT, BUBBLE_LINES);
            if (n == 0u)
                n = 1u, lines[0][0] = '\0';
        }
        meshcore_bubble_head(message, sender, ui->utc_offset_min, ui->eu_dst, head, sizeof(head));
        add_row(ui, &count, 0, message->mine, (int16_t)(m - 1u), head);
        for (size_t l = 0; l < n; l++)
            add_row(ui, &count, l + 1u == n ? 2 : 1, message->mine, (int16_t)(m - 1u), lines[l]);
    }
    return count;
}

/* ---- A text that is edited: lines, a cursor ---- */

static bool line_owns_cursor(const size_t* starts, size_t count, size_t line, size_t cursor)
{
    return starts[line] <= cursor && (line + 1u == count || cursor < starts[line + 1u]);
}

static size_t cursor_line(const size_t* starts, size_t count, size_t cursor)
{
    for (size_t k = 0; k < count; k++)
        if (line_owns_cursor(starts, count, k, cursor))
            return k;
    return count - 1u;
}

/* The line of a text with the bar of the cursor in it */
static void line_with_cursor(const char* text, const size_t* starts, size_t count, size_t line, size_t cursor, char* out, size_t size)
{
    size_t start = starts[line];
    size_t end = line + 1u < count ? starts[line + 1u] : strlen(text);
    size_t n = end - start;
    size_t bar = line_owns_cursor(starts, count, line, cursor) ? cursor - start : n + 1u;
    if (n + 2u > size)
        n = size - 2u;
    size_t o = 0;
    for (size_t i = 0; i <= n; i++)
    {
        if (i == bar)
            out[o++] = '|';
        if (i < n)
            out[o++] = text[start + i];
    }
    out[o] = '\0';
}

/* The first line of a window of `lines` lines that has the cursor in it (the window stays where it is while the cursor is in it) */
static size_t keep_in_window(size_t win, size_t lines, size_t count, size_t line)
{
    if (line < win)
        win = line;
    else if (line >= win + lines)
        win = line + 1u - lines;
    if (win + lines > count && count >= lines)
        win = count - lines;
    return count < lines ? 0u : win;
}

/* The keys of the keyboard at the cursor of the text of the page (the field under the chat, the full-screen one, or the field of the page of a channel) */
static bool edit_text(ui_t* ui, const libdmview_event_t* ev)
{
    char* text = ui->text;
    size_t capacity = sizeof(ui->text);
    size_t* cursor = &ui->cursor;
    if (ui->page == PAGE_ADD)
    {
        text = ui->field == 0 ? ui->name : ui->key;
        capacity = ui->field == 0 ? sizeof(ui->name) : sizeof(ui->key);
        cursor = ui->field == 0 ? &ui->name_cursor : &ui->key_cursor;
    }
    if (strcmp(ev->name, "char") == 0)
        *cursor = meshcore_insert(text, capacity, *cursor, (uint32_t)ev->value);
    else if (strcmp(ev->name, "key") != 0)
        return false;
    else if (ev->value == KEY_BACKSPACE)
        *cursor = meshcore_delete_before(text, *cursor);
    else if (ev->value == KEY_LEFT)
        *cursor = meshcore_cursor_move(text, *cursor, -1);
    else if (ev->value == KEY_RIGHT)
        *cursor = meshcore_cursor_move(text, *cursor, 1);
    else if (ev->value == KEY_HOME)
        *cursor = 0;
    else if (ev->value == KEY_END)
        *cursor = strlen(text);
    else
        return false;
    ui->status[0] = '\0';
    return true;
}

/* A tap on a line of a field: the cursor goes where it was (value: line * 4 + the quarter of the width) */
static void tap_cursor(ui_t* ui, size_t width, size_t win, int32_t value)
{
    size_t starts[EDIT_LINES];
    size_t count = meshcore_edit_lines(ui->text, width, starts, EDIT_LINES);
    size_t line = win + (size_t)(value / 4);
    if (line >= count)
        line = count - 1u;
    size_t end = line + 1u < count ? starts[line + 1u] : strlen(ui->text);
    ui->cursor = meshcore_line_cursor(ui->text, starts[line], end, (unsigned)(value % 4), 4);
    if (line + 1u < count && ui->cursor == end && ui->cursor > starts[line])
        ui->cursor--;                                               /* (the end of a line that goes on is before its space) */
}

/* The field under the chat: two lines of the text (the ones with the cursor), or what happened to the last text while it is empty */
static void show_input(ui_t* ui)
{
    char line[INPUT_WIDTH + 8];
    size_t starts[EDIT_LINES];
    if (ui->text[0] == '\0')
    {
        set_text(ui, "in.l1", ui->status[0] != '\0' ? ui->status : "Write a message");
        set_text(ui, "in.l2", "");
        return;
    }
    size_t count = meshcore_edit_lines(ui->text, INPUT_WIDTH, starts, EDIT_LINES);
    ui->chat_win = keep_in_window(ui->chat_win, 2u, count, cursor_line(starts, count, ui->cursor));
    line_with_cursor(ui->text, starts, count, ui->chat_win, ui->cursor, line, sizeof(line));
    set_text(ui, "in.l1", line);
    if (ui->chat_win + 1u < count)
        line_with_cursor(ui->text, starts, count, ui->chat_win + 1u, ui->cursor, line, sizeof(line));
    else
        line[0] = '\0';
    set_text(ui, "in.l2", line);
}

static void show_chat(ui_t* ui)
{
    char name[24], title[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
    size_t total = build_rows(ui);
    (void)libmeshcore_channel_seen(ui->channel);            /* its texts are on the screen: the notification of the channel goes */
    size_t most = total > CHAT_ROWS ? total - CHAT_ROWS : 0u;
    if (ui->scroll > most)
        ui->scroll = most;
    /* the newest line is the lowest one: with fewer lines than the page has, the top stays empty */
    for (unsigned i = 0; i < CHAT_ROWS; i++)
    {
        long index = (long)total - (long)CHAT_ROWS - (long)ui->scroll + (long)i;
        ui->row_message[i] = -1;
        row_name(name, sizeof(name), "r", i, "v");
        if (index < 0 || (size_t)index >= total)
        {
            set_int(ui, name, 0);
            continue;
        }
        const chat_row_t* row = &ui->rows[index];
        ui->row_message[i] = row->message;
        row_name(name, sizeof(name), "r", i, "t");
        set_text(ui, name, row->text);
        row_name(name, sizeof(name), "r", i, "v");
        set_int(ui, name, (row->mine ? 4 : 1) + row->part);
    }
    if (libmeshcore_channel_name(ui->channel, title, sizeof(title), NULL) != 0)
        Dmod_SnPrintf(title, sizeof(title), "?");
    set_text(ui, "chat.title", title);
    show_input(ui);
}

/* The full-screen field: a text to send */
static void show_compose(ui_t* ui)
{
    char name[40], text[FULL_WIDTH + 8], key[16];
    size_t starts[EDIT_LINES];
    if (libmeshcore_channel_name(ui->channel, name, sizeof(name), NULL) != 0)
        Dmod_SnPrintf(name, sizeof(name), "?");
    set_text(ui, "cp.title", "Message");
    Dmod_SnPrintf(text, sizeof(text), "To: %s", name);
    set_text(ui, "cp.to", text);
    set_text(ui, "cp.hint", "Tap the text to move the cursor");
    size_t count = meshcore_edit_lines(ui->text, FULL_WIDTH, starts, EDIT_LINES);
    ui->full_win = keep_in_window(ui->full_win, FULL_LINES, count, cursor_line(starts, count, ui->cursor));
    for (unsigned i = 0; i < FULL_LINES; i++)
    {
        size_t line = ui->full_win + i;
        if (line < count)
            line_with_cursor(ui->text, starts, count, line, ui->cursor, text, sizeof(text));
        else
            text[0] = '\0';
        Dmod_SnPrintf(key, sizeof(key), "cp.l%u", i);
        set_text(ui, key, text);
    }
    set_text(ui, "cp.status", ui->status);
}

/* A single-line field: the end of the text that fits, with the bar of the cursor when the field is the one that takes the keys */
static void single_line(const char* text, size_t cursor, bool active, char* out, size_t size)
{
    char all[KEY_TYPED_MAX + 4];
    size_t length = strlen(text), o = 0;
    for (size_t i = 0; i <= length && o + 2u < sizeof(all); i++)
    {
        if (active && i == cursor)
            all[o++] = '|';
        if (i < length)
            all[o++] = text[i];
    }
    all[o] = '\0';
    size_t skip = o > FIELD_SHOWN ? o - FIELD_SHOWN : 0u;
    while (all[skip] != '\0' && ((unsigned char)all[skip] & 0xC0u) == 0x80u)
        skip++;
    Dmod_SnPrintf(out, size, "%s", all + skip);
}

/* The page of a new channel: the name and the key, each in a field of its own */
static void show_add(const ui_t* ui)
{
    char line[FIELD_SHOWN + 8];
    set_text(ui, "ad.l1", ui->field == 0 ? "> Name" : "Name");
    set_text(ui, "ad.l2", ui->field == 1 ? "> Key (32 hex digits or base64)" : "Key (32 hex digits or base64)");
    single_line(ui->name, ui->name_cursor, ui->field == 0, line, sizeof(line));
    set_text(ui, "ad.t1", line);
    single_line(ui->key, ui->key_cursor, ui->field == 1, line, sizeof(line));
    set_text(ui, "ad.t2", line);
    set_text(ui, "ad.hint", "A name with # needs no key");
    set_text(ui, "ad.status", ui->status);
}

static void show(ui_t* ui)
{
    set_int(ui, "page", ui->page);
    if (ui->page == PAGE_ADD)
        show_add(ui);
    else if (ui->page == PAGE_COMPOSE)
        show_compose(ui);
    else if (ui->page == PAGE_CHAT)
        show_chat(ui);
    else if (ui->page == PAGE_CHANNELS)
        show_channels(ui);
    else if (ui->page == PAGE_NODES)
        show_nodes(ui);
    else
        show_main(ui);
}

static void open_chat(ui_t* ui, uint8_t channel)
{
    ui->page = PAGE_CHAT;
    ui->channel = channel;
    ui->scroll = 0;
    ui->text[0] = '\0';
    ui->cursor = 0;
    ui->chat_win = 0;
    ui->full_win = 0;
    ui->status[0] = '\0';
    show(ui);
}

/* A tap on a bubble with a link that adds a channel: the channel is added (it is there already: opened) and its chat is shown. false when the text has no link */
static bool open_linked_channel(ui_t* ui, const libmeshcore_message_t* message)
{
    char sender[LIBMESHCORE_NODE_NAME_MAX + 2], name[LIBMESHCORE_CHANNEL_NAME_MAX + 1], secret[LINK_SECRET_MAX];
    const char* body;
    uint8_t index;
    meshcore_split_sender(message->text, sender, sizeof(sender), &body);
    if (libmeshcore_channel_link_parse(body, name, sizeof(name), secret, sizeof(secret)) != 0)
        return false;
    int ret = libmeshcore_channel_find(secret, &index);
    if (ret != 0)
    {
        ret = libmeshcore_channel_add(name, secret);
        if (ret == 0)
            ret = libmeshcore_channel_find(secret, &index);
    }
    if (ret == 0)
    {
        open_chat(ui, index);
        return true;
    }
    Dmod_SnPrintf(ui->status, sizeof(ui->status), ret == -ENOSPC ? "No place for more channels" : ret == -EINVAL ? "Not a key: 24 base64 or 32 hex" : "Could not add (%d)", ret);
    show(ui);
    return true;
}

/* A tap on a line of a bubble of somebody else: the text begins with "@[Sender] " (a bubble with a link that adds a channel adds it instead) */
static void reply_to(ui_t* ui, unsigned row)
{
    if (row >= CHAT_ROWS || ui->row_message[row] < 0)
        return;
    if (open_linked_channel(ui, &ui->messages[ui->row_message[row]]) || ui->messages[ui->row_message[row]].mine)
        return;
    char sender[LIBMESHCORE_NODE_NAME_MAX + 2];
    const char* body;
    meshcore_split_sender(ui->messages[ui->row_message[row]].text, sender, sizeof(sender), &body);
    if (sender[0] == '\0')
        return;
    Dmod_SnPrintf(ui->text, sizeof(ui->text), "@[%s] ", sender);
    ui->cursor = strlen(ui->text);
    ui->status[0] = '\0';
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
        ui->cursor = 0;
        ui->scroll = 0;
        ui->page = PAGE_CHAT;                                       /* (from the full-screen field back to the chat, where the text is now) */
        Dmod_SnPrintf(ui->status, sizeof(ui->status), "Sent");
    }
    else
        Dmod_SnPrintf(ui->status, sizeof(ui->status), ret == -ENODEV ? "No radio" : ret == -EBUSY ? "The channel is busy, try again" : "Could not send (%d)", ret);
    show(ui);
}

/* The name and the key of the page of a new channel: the channel, and the chat of it */
static void add_channel(ui_t* ui)
{
    if (ui->name[0] == '\0' || (ui->name[0] != '#' && ui->key[0] == '\0'))
    {
        Dmod_SnPrintf(ui->status, sizeof(ui->status), ui->name[0] == '\0' ? "Write a name" : "A name without # needs a key");
        ui->field = ui->name[0] == '\0' ? 0 : 1;
        show(ui);
        return;
    }
    int ret = libmeshcore_channel_add(ui->name, ui->key[0] != '\0' ? ui->key : NULL);
    if (ret == 0 || ret == -EEXIST)
    {
        /* the chat of it (the channel is known by the name that was kept: it is cut where it is too long) */
        char kept[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
        Dmod_SnPrintf(kept, sizeof(kept), "%s", ui->name);
        for (uint8_t i = 0; i < LIBMESHCORE_MAX_CHANNELS; i++)
        {
            char have[LIBMESHCORE_CHANNEL_NAME_MAX + 1];
            if (libmeshcore_channel_name(i, have, sizeof(have), NULL) == 0 && strcmp(have, kept) == 0)
            {
                open_chat(ui, i);
                return;
            }
        }
    }
    Dmod_SnPrintf(ui->status, sizeof(ui->status), ret == -ENOSPC ? "No place for more channels" : ret == -EINVAL ? "Not a key: 24 base64 or 32 hex" : "Could not add (%d)", ret);
    show(ui);
}

static void handle_add(ui_t* ui, const libdmview_event_t* ev)
{
    if (strcmp(ev->name, "key") == 0 && ev->value == KEY_ENTER)
    {
        if (ui->field == 0 && !(ui->name[0] == '#' && ui->key[0] == '\0'))
            ui->field = 1;                                          /* the name is written: the key is next (a name with # needs none) */
        else
        {
            add_channel(ui);
            return;
        }
    }
    else if (strcmp(ev->name, "add") == 0)
    {
        add_channel(ui);
        return;
    }
    else if (strcmp(ev->name, "sel") == 0)
        ui->field = ev->value == 1 ? 1 : 0;
    else if (strcmp(ev->name, "back") == 0)
    {
        ui->page = PAGE_CHANNELS;
        ui->status[0] = '\0';
    }
    else if (!edit_text(ui, ev))
        return;
    show(ui);
}

static void handle_compose(ui_t* ui, const libdmview_event_t* ev)
{
    if ((strcmp(ev->name, "key") == 0 && ev->value == KEY_ENTER) || strcmp(ev->name, "send") == 0)
    {
        send_text(ui);
        return;
    }
    if (strcmp(ev->name, "back") == 0)
    {
        ui->page = PAGE_CHAT;
        ui->status[0] = '\0';
    }
    else if (strcmp(ev->name, "cf") == 0)
        tap_cursor(ui, FULL_WIDTH, ui->full_win, ev->value);
    else if (!edit_text(ui, ev))
        return;
    show(ui);
}

static void handle_chat(ui_t* ui, const libdmview_event_t* ev)
{
    if ((strcmp(ev->name, "key") == 0 && ev->value == KEY_ENTER) || strcmp(ev->name, "send") == 0)
    {
        send_text(ui);
        return;
    }
    if (strcmp(ev->name, "older") == 0 || (strcmp(ev->name, "key") == 0 && ev->value == KEY_UP))
        ui->scroll += SCROLL_STEP;
    else if (strcmp(ev->name, "newer") == 0 || (strcmp(ev->name, "key") == 0 && ev->value == KEY_DOWN))
        ui->scroll = ui->scroll >= SCROLL_STEP ? ui->scroll - SCROLL_STEP : 0u;
    else if (strcmp(ev->name, "edit") == 0)
        ui->page = PAGE_COMPOSE;
    else if (strcmp(ev->name, "cc") == 0)
        tap_cursor(ui, INPUT_WIDTH, ui->chat_win, ev->value);
    else if (strcmp(ev->name, "brow") == 0)
    {
        reply_to(ui, (unsigned)ev->value);
        return;
    }
    else if (strcmp(ev->name, "back") == 0)
    {
        ui->page = PAGE_CHANNELS;
        ui->first = 0;
        ui->status[0] = '\0';
    }
    else if (!edit_text(ui, ev))
        return;
    show(ui);
}

static void handle_lists(ui_t* ui, const libdmview_event_t* ev)
{
    size_t rows = ui->page == PAGE_NODES ? NODE_ROWS : LIST_ROWS;
    if (strcmp(ev->name, "new") == 0 && ui->page == PAGE_CHANNELS)
    {
        ui->page = PAGE_ADD;
        ui->name[0] = ui->key[0] = '\0';
        ui->name_cursor = ui->key_cursor = 0;
        ui->field = 0;
        ui->status[0] = '\0';
    }
    else if (strcmp(ev->name, "row") == 0 && ui->page == PAGE_CHANNELS)
    {
        size_t index = ui->first + (size_t)ev->value;
        if (index < ui->listed_count)
            open_chat(ui, ui->listed[index]);
        return;
    }
    else if (strcmp(ev->name, "open.nodes") == 0)
    {
        ui->page = PAGE_NODES;
        ui->first = 0;
    }
    else if (strcmp(ev->name, "open.chats") == 0)
    {
        ui->page = PAGE_CHANNELS;
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
    if (ui->page == PAGE_NODES || ui->page == PAGE_CHANNELS)
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
            list_channels(ui);
            total = ui->listed_count;
        }
        if (ui->first >= total && total > 0)
            ui->first = (total - 1u) / rows * rows;
        else if (total == 0)
            ui->first = 0;
    }
    show(ui);
}

static void handle(ui_t* ui, const libdmview_event_t* ev)
{
    if (ui->page == PAGE_ADD)
        handle_add(ui, ev);
    else if (ui->page == PAGE_COMPOSE)
        handle_compose(ui, ev);
    else if (ui->page == PAGE_CHAT)
        handle_chat(ui, ev);
    else
        handle_lists(ui, ev);
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
    ui.messages = Dmod_Malloc(sizeof(*ui.messages) * LIBMESHCORE_MAX_MESSAGES);
    ui.rows = Dmod_Malloc(sizeof(*ui.rows) * MAX_ROWS);
    if (ui.messages == NULL || ui.rows == NULL)
    {
        Dmod_Free(ui.messages);
        Dmod_Free(ui.rows);
        return -ENOMEM;
    }
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
        Dmod_Free(ui.messages);
        Dmod_Free(ui.rows);
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
    Dmod_Free(ui.messages);
    Dmod_Free(ui.rows);
    return 0;
}
