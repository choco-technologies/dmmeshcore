# dmmeshcore

The **MeshCore** mesh protocol ([MeshCore](https://github.com/meshcore-dev/MeshCore)) for DMOD: the format of its packets, the adverts of the nodes, the
group channels. The library does not touch the radio: a client reads frames from a LoRa node (`DMDRVI_IOCTL_LORA_RECV` of dmdrvi, e.g. the
[dmsx126x](https://github.com/choco-technologies/dmsx126x) of the T-Deck Pro), gives them to it and sends what it builds.

## What is there

| | |
|---|---|
| `dmmeshcore_packet_decode / _encode` | a frame <-> a packet: the route (flood, direct, with/without transport codes), the type, the version, the path (hashes of 1..3 bytes), the payload |
| `dmmeshcore_advert_decode` | an advert: the public key, the time, the signature (not verified yet), the kind of the node, the place, the name |
| `dmmeshcore_channel_init / _channel_from_text` | a group channel from its 16-byte secret (base64 as the apps show it, or 32 hex digits); its hash is the first byte of SHA-256 of the secret (the public channel, `izOH6cXN6mrJ5e26oRXNcg==`, is 0x11) |
| `dmmeshcore_group_text_decode / _encode` | a group text: `[channel hash][2 bytes of HMAC-SHA256][AES-128-ECB(time, flags, text)]` through `dmcrypto` |

Not there yet (the next steps): the identity of the node (ed25519: the signature of adverts, the shared secret of direct messages), the routing
(the flood with the hashes of the repeaters, the table of the seen packets, the paths), the contacts, the delivery of the messages.

## The tool

`meshcore [-d NODE] [-k KEY]... sniff [SECONDS]` listens on `/dev/lora` and says what comes:

```
-47 dBm snr 11  109 bytes  flood group-text v0  4 hops (2-byte hashes: 4c16 ee9f e15f 5ad8)  payload 99
    channel #ca (no key)
-51 dBm snr 11  63 bytes  flood advert v0  2 hops (...)  payload 118
    repeater "Hill"  key 3fa1c0de..  time 1763200000  at 50.123456, 19.654321
```

The group texts are decrypted for the channels it has the key of (`-k`; the public channel is always tried).

## Build and test

```sh
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/x86_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The test frames are frames heard on 869.618 MHz with a T-Deck Pro. [MIT license](LICENSE)
