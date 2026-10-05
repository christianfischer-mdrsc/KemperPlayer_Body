# STM32 USB Host Library (Core)

Quelle: https://github.com/STMicroelectronics/stm32-mw-usb-host, Tag **v3.5.3**,
nur der Ordner `Core` (ohne Klassen und ohne die `*_template`-Dateien).
Lizenz: siehe `LICENSE.md` (ST, BSD-3-Clause).

Die Klasse fuer USB-MIDI und die Konfiguration (`usbh_conf.c/.h`) liegen im
Projekt unter `firmware/CM7/USB/`.

## Aenderung gegenueber dem Original

`Core/Src/usbh_core.c`, Zustand `HOST_CHECK_CLASS` (markiert mit
`KPD-Patch`): Die Originalversion prueft nur die Klasse von **Interface 0**.
Der Kemper Player meldet sich als zusammengesetztes Geraet (Audio, MIDI,
Rig Manager); das MIDI-Interface ist dort nicht zwingend das erste. Der
Patch durchsucht alle gelesenen Interfaces. Beim Aktualisieren der Library
diese Stelle wieder einbauen.
