# libmeshcore API

The reference is the header, [include/libmeshcore.h](../include/libmeshcore.h): the protocol (`libmeshcore_packet_*`, `libmeshcore_advert_decode`,
`libmeshcore_channel_*`, `libmeshcore_group_text_*`), the service (`libmeshcore_status`, `_wait`, `_contacts`, `_messages`, `_log`, `_send_text`) and the settings
(`libmeshcore_settings_get/_set`, `libmeshcore_preset`).

The service is a library service of libsystemd (`type=library`, `configs/libmeshcore@.ini`): it runs for the radio that libsystemd was told about (the class `lora`).
