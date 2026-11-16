# meshcore - the screen of the MeshCore mesh

The application of the launcher: a page (`views/meshcore.html`, converted by dmod into a view) over [libmeshcore](../../README.md). The contract between the page and
`src/main.c` (properties the application sets, events the page emits): `page`; `st.title`, `st.line1..3` (the state); `n0..n4.on|name|sub` and `nodes.info` (the nodes);
`t0..t3.on|who|l1|l2` and `texts.info` (the texts); events `open.nodes`, `open.texts`, `back`, `prev`, `next`.
