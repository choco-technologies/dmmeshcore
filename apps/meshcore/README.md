# meshcore - the screen of the MeshCore mesh

The application of the launcher: a page (`views/meshcore.html`, converted by dmod into a view) over [libmeshcore](../../README.md). The pages: the state, the
nodes, the channels, a chat (the texts of one channel in bubbles - the texts of others at the left, of this node at the right in black, the newest at the bottom, the
older ones are above: the `^` and `v` of the header, or the arrows of the keyboard, scroll; a tap on a bubble of somebody else starts a reply `@[Sender] `; the field
under the chat takes the keys, `[ ]` opens it full-screen, `>` or Enter sends) and a full-screen field (a long text, or the name of a new channel: `#test`, or `Name KEY`).

The contract between the page and `src/main.c` (properties the application sets, events the page emits): `page` (0 main, 1 nodes, 2 channels, 3 chat, 4 field);
`st.title`, `st.line1..3` (the state); `n0..n4.on|name|sub` and `nodes.info` (the nodes); `c0..c4.on|name|sub` and `chats.info` (the channels); `chat.title`,
`in.l1|l2` (the field under the chat), `r0..r11.v|t` (the 12 lines of the chat of 16 px: `v` is 0 for none, 1-3 the head, a middle line and the last line of a bubble of
somebody else, 4-6 of this node; `t` is the text); `cp.title|to|text|hint|status` (the full-screen field); events `open.nodes`, `open.chats`, `back`, `prev`, `next`,
`new`, `row` (a channel of the list), `brow` (a line of the chat), `older`, `newer`, `edit`, `send`, and the keys (`char`, `key`).

A page that is converted has one radius for all the corners of a box, so the round ends of a bubble are a rounded box taller than the line that the line cuts.
