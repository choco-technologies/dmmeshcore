# dmmeshcore

The **MeshCore** mesh ([MeshCore](https://github.com/meshcore-dev/MeshCore)) for DMOD. Three modules:

| module | what |
|--------|------|
| **`libmeshcore`** (the root of the repo) | **everything but the screen**: the protocol (packets, adverts, group channels), the service that listens on the radio, what it heard (nodes, texts, a log), the settings of the radio (kept on the device). Applications are a view of it. |
| `meshcore` (`apps/meshcore`) | the application of the launcher (it has an icon: phoneui lists it): the state, the nodes, the channels and their chats. Only a screen over libmeshcore. |
| `mesh` (`tools/mesh`) | the command line: a thin shell over libmeshcore (`mesh nodes`, `mesh texts`, `mesh set preset=1`, `mesh send ...`). |

LoRa settings are also in **Settings** (the page LoRa: the switch, the region, the power), like the Wi-Fi ones.

## How it starts

dmdevfs reports a radio (a node that answers `DMDRVI_IOCTL_LORA_GET_INFO`, e.g. `/dev/lora` of [dmsx126x](https://github.com/choco-technologies/dmsx126x)) to libsystemd
as a device of the class `lora`. `configs/libmeshcore.rules` (`[class=lora] start=libmeshcore@%name`) makes libsystemd start the library service for it
(`configs/libmeshcore@.ini`, `type=library`): nothing is started by hand. In the image:

```
libmeshcore service=libmeshcore@.ini
libmeshcore rules=libmeshcore.rules
mesh
meshcore @screen epaper-240x320
```

The service keeps a thread on the radio: it takes every frame apart, remembers the nodes (adverts, 16), the texts of the channels it has the key of (32) and a log of the
packets (16). `libmeshcore_wait()` tells an application when something changed, so a screen does not poll.

## libmeshcore

```c
libmeshcore_status(&status);                   // running, listening, packets, adverts, texts ...
libmeshcore_contacts(c, n, &count);            // the nodes heard, the newest first
libmeshcore_messages(m, n, &count);            // the texts of the channels
libmeshcore_settings_get(&s); libmeshcore_settings_set(&s);   // preset, power, name ...: checked, kept in /ext/meshcore.ini, applied to the radio
libmeshcore_channel_add("Friends", "base64 or 32 hex digits");   // or ("#test", NULL): the key of a channel of a name is made of the name
libmeshcore_channel_remove(index);             // the channels are kept in /ext/meshcore-channels.ini
libmeshcore_send_text(channel, "hello");       // a flood to a channel (after a look at the channel: CAD)
libmeshcore_packet_decode / _encode, _advert_decode, _group_text_decode / _encode   // the protocol, without a radio
```

The presets: EU/UK narrow (869.618 MHz, 62.5 kHz, SF8, 4/8 - the one the mesh around the T-Deck Pro uses), EU/UK long range, USA/Canada, custom (`mesh set frequency=... sf=...`).

Not there yet: the identity of the node (ed25519: signed adverts, direct messages), the routing (repeating, the table of seen packets, paths), keeping the keys of channels
in dmsecrets (now they are in /ext/meshcore-channels.ini), and checking the signature of an advert.

## Build and test

```sh
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/x86_64
cmake --build build --parallel
```

The tests are modules (`test_libmeshcore`, `test_meshcore`) run with `dmod_loader` (see `.github/workflows/ci.yml`). The test frames are frames heard on 869.618 MHz with a T-Deck Pro. [MIT license](LICENSE)
