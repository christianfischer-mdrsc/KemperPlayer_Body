# Kemper Player Display

Eigenbau-Controller für den **Kemper Profiler Player**: 7"-Touch-Display (Riverdi, STM32H757) mit sechs Footswitches (3 oben, 3 unten). Die Oberfläche orientiert sich am Rig Manager und läuft mit **LVGL** direkt auf dem Display.

## Vorschau

**Live-Ansicht** (zum Spielen, große Schrift) – so startet das Display ohne Kemper, alle Rigs sind leer:

![Live-Ansicht](docs/images/vorschau_live.png)

Dieselbe Ansicht, nachdem am Display Effekte gewählt, das Rig umbenannt und das Tempo getippt wurde:

![Live-Ansicht mit Effekten](docs/images/vorschau_live_effekte.png)

**Bearbeiten-Ansicht** (zum Einstellen) und der Dialog eines Effektmoduls:

![Bearbeiten-Ansicht](docs/images/vorschau_bearbeiten.png)

![Modul-Dialog](docs/images/vorschau_modul.png)

**Bank-Übersicht**, **Tuner** und **System**:

![Bank-Übersicht](docs/images/vorschau_banks.png)

![Tuner](docs/images/vorschau_tuner.png)

![System](docs/images/vorschau_system.png)

Alle Bilder sind echte Renderings aus LVGL mit genau dem Code in diesem Repository (PC-Simulation mit simulierten Touch-Eingaben).

**Startbildschirm** mit Fortschritt und gemessener Bootzeit:

![Startbildschirm beim Laden](docs/images/vorschau_start_laden.png)

![Startbildschirm fertig](docs/images/vorschau_start_fertig.png)

## Interaktiver HTML-Prototyp

Im Ordner [`prototype/`](prototype/) liegt die komplette Bedienoberfläche als HTML zum Ausprobieren, inklusive Startseite, Performance-Browser, Bank-Übersicht, Effekt-Details, Tuner, Einstellungen, Stomp-Belegung, Morph, Setlist-Ansicht, Setlist Manager und Song-Editor (Teile wie Intro, Verse, Solo mit eigenem Slot, Bildschirmtastatur):

| Datei | Inhalt |
|---|---|
| [`kemper-display.html`](prototype/kemper-display.html) | Nur das Display, füllt das ganze Browserfenster |
| [`kemper-display-mit-hilfe.html`](prototype/kemper-display-mit-hilfe.html) | Display mit Gehäuserahmen und Bedienhinweisen |

**Öffnen:** Da das Repository privat ist, zeigt GitHub HTML-Dateien nur als Quelltext an. So geht es:

1. Datei in GitHub öffnen und oben rechts auf **Download raw file** tippen
2. Die heruntergeladene Datei im Browser öffnen (am Handy im Querformat)

Oder das Repository klonen und die Datei per Doppelklick öffnen. Eigene Effekt-Icons kannst du in einen Ordner `prototype/icons/` legen (Details in der Hilfe-Version).

## Firmware bauen und aufspielen

Basis ist der offizielle LVGL-Port für das Riverdi-7"-Display ([lvgl/lv_port_riverdi_70-stm32h7](https://github.com/lvgl/lv_port_riverdi_70-stm32h7)), passend für das **RVT70HSSNWC00-B**. Statt der LVGL-Demo startet die Kemper-Oberfläche.

**Benötigt:** STM32CubeIDE, ST-LINK (V2/V3) oder J-Link, Netzteil 6–48 V (z. B. 9 V).

1. Repository klonen
2. STM32CubeIDE öffnen: **File → Open Projects from File System… → Directory** → Ordner `firmware/STM32CubeIDE` wählen → **Finish**
3. Im Project Explorer das Unterprojekt **`…_CM7`** auswählen
4. **Project → Build Project** (für beste Leistung Konfiguration *Release*)
5. Display mit Netzteil versorgen, Debugger an den SWD-Anschluss, dann **Run** zum Flashen

Beim Einschalten erscheint der Startbildschirm und zeigt Fortschritt und Bootzeit (Millisekunden seit Reset). Danach blendet er zur Live-Ansicht über.

## Bedienung (ohne Kemper)

Die Oberfläche bildet nur den **Kemper Profiler Player** ab: Banks mit je 5 Rigs (keine Performances), Bankfarben wie die LEDs am Player, Effect Buttons I–IIII. Ohne Kemper sind alle Rigs leer; alles, was am Display geändert wird, landet im Datenmodell und wird später per USB-MIDI an den Kemper geschickt.

| Ansicht | Was funktioniert |
|---|---|
| **Live** | Bank wechseln (Pfeile), Bank antippen → Bank-Übersicht, Effektmodul antippen → ein/aus, Tempo antippen → Tap Tempo, „bearbeitet“-Hinweis wie am Kemper |
| **Footswitch-Leiste** | Modus *Rigs*: FS1–5 = Rig 1–5, FS6 → Effekte. Modus *Effekte*: FS1–4 = Effect Button I–IIII, FS5 = Tap, FS6 → Rigs |
| **Bearbeiten** | Modul antippen → Effekttyp, Ein/Aus, Zuweisung zu Effect Buttons; RIG/INPUT/AMP/EQ/CAB/OUTPUT → Regler; Rig umbenennen (Bildschirmtastatur); Morph; Tempo ein/aus, ± |
| **Banks** | Banks seitenweise (je 10), Rig antippen → laden |
| **Tuner** | Referenz 424–456 Hz, Stummschaltung; Ton und Abweichung kommen vom Kemper |
| **System** | Player-Level I/II/III, Footswitch-Modus, **Helligkeit** (PWM), **Uhr stellen** (RTC), Info |

Signalkette je Ausbaustufe: Level I/II `A, B | Stack | DLY, REV` mit 10 Banks; Level III `A, B, C, D | Stack | X, MOD, DLY, REV` mit 125 Banks.

**Hinweise:** Die eingebauten LVGL-Fonts enthalten keine Umlaute, deshalb kommen in der Oberfläche keine vor. Die Wertebereiche der Parameter sind vorläufig und werden bei der MIDI-Anbindung an die Kemper-Werte angeglichen. Einstellungen werden noch nicht dauerhaft gespeichert.

## Footswitches anschließen

Taster (Schließer) zwischen Pin und GND am Expansion-Header P8 (1,27 mm). Der interne Pull-up ist an; ein externer Pull-up (4,7 kΩ auf **3,3 V**) darf zusätzlich vorhanden sein. **Nie 5 V an die Pins** (Header-Pins 1 und 3 führen 5 V).

| Footswitch | MCU | Header-Pin |
|---|---|---|
| FS1 | PD11 | 10 |
| FS2 | PB10 | 12 |
| FS3 | PD12 | 13 |
| FS4 | PD13 | 15 |
| FS5 | PB11 | 34 |
| FS6 | PH4 | 36 |
| GND | | 6, 7, 17, 18, 28 |

Ohne Verdrahtung testen: Der Taster **BTN1** auf dem Board (PC6) wirkt wie FS1. Treiber: `CM7/Core/Src/footswitch.c` (Abfrage alle 5 ms, Druck wird sofort gemeldet, danach 30 ms Entprellen; lang gedrückt ab 800 ms über `footswitch_long_press()`). Pinbelegung laut Riverdi-Datenblatt Rev. 1.1 – bei neueren Board-Revisionen bitte gegenprüfen.

**Uhrzeit:** Beim allerersten Start stellt die Firmware den RTC auf den Zeitpunkt des Builds (`__DATE__`/`__TIME__`) und merkt sich das im Backup-Register. Der RTC läuft derzeit mit dem internen LSI-Oszillator, geht also ungenau und behält die Zeit ohne Versorgung nicht. Für eine dauerhaft richtige Uhr braucht es LSE (32,768-kHz-Quarz) und eine Pufferung an VBAT.

## Kemper-Verbindung (USB)

Das Display ist ein **USB-MIDI-Gerät** (class compliant, wie MIDI Captain & Co.) und hängt an der **USB-A-Buchse des Players**. Der Player ist USB-Host. Die USB-B-Buchse des Players bleibt frei für Rig Manager und USB-Audio am Rechner.

**Anschluss:** Die USB-Schnittstelle des Riverdi-Boards ist der 5-polige Molex-Stecker **P10 „USB“** (1,25 mm): 1 = VCC_USB, 2 = D−, 3 = D+, 4 = ID, 5 = GND. **Pin 4 (ID) bleibt offen** (= Device-Modus laut Riverdi-Datenblatt). D−, D+ und GND auf einen USB-A-Stecker zum Player. VCC_USB (Pin 1) kann ebenfalls angeschlossen werden, die Firmware nutzt die 5 V des Players aber nicht; das Display läuft über sein eigenes Netzteil.

> **Vor dem ersten Anstecken:** Mit eingeschaltetem Display und *ohne* Kabel an P10 Pin 1 gegen GND messen. Dort sollten **0 V** anliegen (der VBUS-Schalter USB1_EN/PF10 für den Host-Modus bleibt aus). Liegen 5 V an, VCC_USB nicht mit dem Player verbinden und Bescheid geben.

**Was die Firmware macht** (`firmware/CM7/USB/`):

1. Das Display meldet sich am Player als MIDI-Gerät „Kemper Player Display“ an.
2. Es schaltet den Player mit dem Kemper-„Beacon“ in den bidirektionalen SysEx-Modus. Der Player schickt dann etwa alle 500 ms ein Lebenszeichen und meldet Änderungen (z. B. Rig-Name) von selbst.
3. Als Test der Übertragung werden abgefragt (alle 3 s neu): **Name von Rig 1 in Bank 1**, Name des aktuellen Rigs, Amp, Cab und Rig-Tempo. Der Name von Rig 1 landet auch in der Bank-Übersicht und auf FS1; Amp, Cab und Tempo in der Live-Ansicht.
4. Zusätzlich fragt das Display per MIDI-Identity-Request nach einer Firmware-Kennung. Kemper dokumentiert diese Antwort nicht; kommt keine, steht dort „vom Kemper nicht gemeldet“.

Die Abfrage für Rig 1 nutzt die erweiterte String-Adresse `00 00 01 00 01` (Funktion 0x47). Sie ist nicht offiziell dokumentiert, laut Kemper-Forum liefert der Player damit aber den Namen von Bank 1 / Rig 1. Kommt keine Antwort, zeigt die Zeile „keine Antwort“ – der aktuelle Rig-Name (offiziell dokumentiert) bestätigt die Übertragung dann trotzdem.

**Überwachung:** Kabel ab oder Player aus wird über den USB-Zustand erkannt. Bleiben die Lebenszeichen 1,5 s aus, gilt die Verbindung als unterbrochen; das Display versucht dann alle 2 s neu zu verbinden. Die Statusleiste zeigt „Kemper verbunden“ nur, solange Lebenszeichen kommen.

**Anzeige:** *System* (Zahnrad) → Abschnitt **Kemper-Verbindung**: Status mit Verbindungsdauer, USB-Zustand, Rig 1 (Bank 1), aktuelles Rig, Amp/Cab, Tempo, Firmware, Alter des letzten Lebenszeichens und Zähler für Nachrichten und Unterbrechungen.

**Senden von Aktionen** (nächster Schritt): Rig laden per Program Change, Effekte per CC 17–29, Tap per CC 30, Tuner per CC 31, Parameter per SysEx. Dafür werden die vorbereiteten `kp_link_*`-Funktionen in `kemper_link.c` gefüllt.

**USB-Kennung:** VID/PID 0x1209/0x0001 (von pid.codes für private Tests freigegeben). Für eine Weitergabe des Geräts bräuchte es eine eigene PID.

**CubeMX:** Die USB-Teile sind von Hand eingebunden. Wer das Projekt mit CubeMX neu erzeugt, muss in `CM7/Core/Inc/stm32h7xx_hal_conf.h` wieder `HAL_PCD_MODULE_ENABLED` setzen (oder in CubeMX USB_OTG_HS auf *Device_Only* stellen, ohne die ST-Middleware „USB_DEVICE“ zu aktivieren).

## Aufbau des Repositorys

```
firmware/                     STM32CubeIDE-Projekt (Basis: LVGL-Riverdi-Port)
├── CM7/UI/                   Kemper-Oberfläche
│   ├── ui_boot.c / .h        Startbildschirm mit Fortschritt und Bootzeit
│   ├── kemper_player.c / .h  Datenmodell des Players (Banks, Rigs, Module, Effect Buttons,
│   │                         Parameter, Tempo, Tuner, Footswitches); kp_link_* = Richtung
│   │                         Kemper (noch leer), kp_rx_* = Daten vom Kemper
│   ├── ui_common.c / .h      Stile, Navigation, Effektkette, Footswitch-Leiste, Dialoge
│   ├── ui_live.c / .h        Live-Ansicht, Einstieg ui_start(), ui_footswitch()
│   ├── ui_edit.c             Bearbeiten-Ansicht, Modul-Dialog, Umbenennen
│   ├── ui_banks.c            Bank-Übersicht
│   ├── ui_tuner.c            Tuner
│   ├── ui_settings.c         System (Level, Footswitch-Modus, Helligkeit, Kemper-Verbindung, Uhr, Info)
│   ├── ui_statusbar.c / .h   Statusleiste: USB-Status, Datum, Uhrzeit
│   └── xml/                  Frühere Ansichten als LVGL-XML (veraltet, nur Referenz)
├── CM7/USB/                  Verbindung zum Kemper
│   ├── usbd_conf.c / .h      USB-Gerät auf USB_OTG_HS (internes FS-PHY), FIFOs
│   ├── usbd_desc.c / .h      Geräte- und String-Deskriptoren
│   ├── usbd_midi.c / .h      USB-MIDI-Klasse (MIDI 1.0), Empfangspuffer, Senden
│   └── kemper_link.c / .h    Eigener Task: SysEx-Protokoll, Beacon, Verbindungsüberwachung,
│                             Testdaten; Übergabe an das Modell im LVGL-Task
├── Middlewares/ST/STM32_USB_Device_Library/   ST USB Device Library v2.11.3 (Core)
├── CM7/Core/Src/main.c       Startbildschirm, Init-Schritte, dann ui_start()
├── CM7/Core/Src/footswitch.c Footswitches am Expansion-Header (Entprellen, langer Druck)
├── CM7/Core/Src/rtc.c        RTC als Zeitquelle für die Statusleiste, Uhr stellen
├── Middlewares/Third_Party/LVGL/lv_conf.h   Fonts aktiviert, Demos aus
└── STM32CubeIDE/             Projektdateien (CM7 und CM4)
prototype/                    Interaktiver HTML-Prototyp
docs/                         Konzept, Einkaufsliste, Kemper-Protokoll, Bilder
```

## Nächste Schritte

- [x] USB-MIDI-Gerät (M7) am USB-A des Players, Verbindungsüberwachung, erste Daten vom Kemper
- [x] Datenmodell des Players (Banks, Rigs, Module, Effect Buttons) und Anbindung an die Oberfläche
- [x] Screens: Bank-Übersicht, Tuner, System, Modul- und Parameter-Dialoge
- [ ] kp_link_* / kp_rx_* vollständig mit USB-MIDI füllen (Rig laden, Effekte, Tuner, Bank/Slot) – bisher: Verbindung, Rig 1, Amp/Cab, Tempo
- [x] Footswitches über den 40-Pin-Header
- [ ] LED-Ringe (SK6812) an den Footswitches
- [ ] Fonts mit Umlauten, Einstellungen dauerhaft speichern
- [ ] Startseite, Setlist-Bereich und Setlist Manager

Mehr zum Konzept in [`docs/konzept.md`](docs/konzept.md).

## Hinweise

- Der Riverdi/LVGL-Port nutzt STM32Cube-HAL (BSD-3), FreeRTOS (MIT) und LVGL (MIT). Die Original-Anleitung des Ports liegt unter [`firmware/README.md`](firmware/README.md).
- Um das Repository schlank zu halten, wurden aus LVGL die Ordner `tests`, `docs`, `scripts`, `examples` und `demos` entfernt. Sie werden für den Build nicht gebraucht.
