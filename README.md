# Kemper Player Display

Eigenbau-Controller für den **Kemper Profiler Player**: 7"-Touch-Display (Riverdi, STM32H757) mit sechs Footswitches und vier Drehgebern. Die Oberfläche orientiert sich am Rig Manager und läuft mit **LVGL** direkt auf dem Display.

## Vorschau

**Live-Ansicht** (zum Spielen, große Schrift):

![Live-Ansicht](docs/images/vorschau_live.png)

**Bearbeiten-Ansicht** (zum Einstellen):

![Bearbeiten-Ansicht](docs/images/vorschau_bearbeiten.png)

Beide Bilder sind echte Renderings aus LVGL mit genau dem Code in diesem Repository.

**Startbildschirm** mit Fortschritt und gemessener Bootzeit:

![Startbildschirm beim Laden](docs/images/vorschau_start_laden.png)

![Startbildschirm fertig](docs/images/vorschau_start_fertig.png)

## Interaktiver HTML-Prototyp

Im Ordner [`prototype/`](prototype/) liegt die komplette Bedienoberfläche als HTML zum Ausprobieren, inklusive Performance-Browser, Bank-Übersicht, Effekt-Details, Tuner, Einstellungen, Stomp-Belegung und Morph:

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

Beim Einschalten erscheint der Startbildschirm und zeigt Fortschritt und Bootzeit (Millisekunden seit Reset). Danach blendet er zur Live-Ansicht über. Über **Bearbeiten** oben rechts wechselst du in die Bearbeiten-Ansicht, über den blauen **Play-Button** zurück. Sonst hat die Oberfläche noch keine Funktion.

## Aufbau des Repositorys

```
firmware/                     STM32CubeIDE-Projekt (Basis: LVGL-Riverdi-Port)
├── CM7/UI/                   Kemper-Oberfläche
│   ├── ui_boot.c / .h        Startbildschirm mit Fortschritt und Bootzeit
│   ├── ui_live.c / .h        Live- und Bearbeiten-Ansicht in C (ui_start())
│   └── xml/                  Dieselben Ansichten als LVGL-XML (Referenz)
├── CM7/Core/Src/main.c       Startbildschirm, Init-Schritte, dann ui_start()
├── Middlewares/Third_Party/LVGL/lv_conf.h   Fonts aktiviert, Demos aus
└── STM32CubeIDE/             Projektdateien (CM7 und CM4)
prototype/                    Interaktiver HTML-Prototyp
docs/                         Konzept, Einkaufsliste, Kemper-Protokoll, Bilder
```

## Nächste Schritte

- [ ] USB-MIDI-Device auf dem M4-Kern (TinyUSB), erste Kemper-Befehle
- [ ] Datenmodell (Performance, Slots, Effekte) und Anbindung an die Oberfläche
- [ ] Footswitches, Drehgeber und LEDs über den 40-Pin-Header
- [ ] Weitere Screens: Effekt-Detail, Browser, Bank-Übersicht, Tuner, Einstellungen

Mehr zum Konzept in [`docs/konzept.md`](docs/konzept.md).

## Hinweise

- Der Riverdi/LVGL-Port nutzt STM32Cube-HAL (BSD-3), FreeRTOS (MIT) und LVGL (MIT). Die Original-Anleitung des Ports liegt unter [`firmware/README.md`](firmware/README.md).
- Um das Repository schlank zu halten, wurden aus LVGL die Ordner `tests`, `docs`, `scripts`, `examples` und `demos` entfernt. Sie werden für den Build nicht gebraucht.
