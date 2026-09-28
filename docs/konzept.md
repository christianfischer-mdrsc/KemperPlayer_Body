# Konzept: Kemper Player Display

Eigenbau-Controller mit 7"-Touch-Display und Footswitches für den Kemper Profiler Player.
Kommunikation über USB-MIDI (der Player ist USB-Host, das Display ein USB-MIDI-Gerät).

## Hardware

| Baugruppe | Auswahl |
|---|---|
| Display + MCU | Riverdi RVT70HSSNWC00-B: 7", 1024 × 600, IPS, Optical Bonding, kapazitiver Touch, STM32H757 (M7 + M4), 8 MB SDRAM, 64 MB QSPI |
| Footswitches | 6 Stück in einer Reihe, 64 mm Abstand, Soft-Touch mit RGB-LED-Ring (SK6812) |
| Drehgeber | 4 × ALPS EC11E mit Taster, je zwei links und rechts neben dem Display |
| Anschlüsse | USB-C (verriegelbar) zum Player, 9 V DC (Pedalboard-Standard), optional Expression-Pedal |
| Gehäuse | ca. 380 × 210 × 45 mm, Alu CNC, eloxiert |

Das Riverdi-Modul braucht 6–48 V. Das 9-V-Netzteil ist deshalb Pflicht, USB dient nur für Daten.

## Aufgabenverteilung im STM32H757

- **M7-Kern:** Oberfläche mit LVGL
- **M4-Kern:** USB-MIDI, Kemper-SysEx-Protokoll, Footswitches, Drehgeber, LEDs, Expression-Pedal

## Einkaufsliste (Prototyp, Richtwerte)

| Komponente | Preis ca. |
|---|---|
| Riverdi RVT70HSSNWC00-B | 210–230 € |
| ST-LINK V3MINIE | 15 € |
| 6 × Footswitch (Soft-Touch, Metall) | 25–40 € |
| 6 × SK6812 + Lichtleiterringe | 10–20 € |
| 4 × ALPS EC11E + Alu-Knöpfe | 30–70 € |
| USB-C-Einbaubuchse (verriegelbar), DC-Buchse, Klinkenbuchse | 25–35 € |
| Schutz- und Power-Bauteile | 10–15 € |
| Trägerplatine (4 Lagen, bestückt) | 40–80 € |
| Gehäuse: 3D-Druck-Prototyp / Alu CNC | 20–50 € / 150–400 € |

## Kemper-Kommunikation (Kurzfassung)

SysEx-Aufbau: `F0 00 20 33 02 7F <Funktion> 00 <Seite> <Nummer> [Wert MSB LSB] F7`

| Funktion | Bedeutung |
|---|---|
| `01` | Parameter setzen |
| `41` | Parameter abfragen (Antwort mit `01`) |
| `43` | Text abfragen, z. B. Rig-Name (Antwort mit `03`) |
| `47` | Erweiterter Text, z. B. Slot-Namen |
| `7E` | Beacon für automatische Rückmeldungen |

Beispiele:
- Rig-Name abfragen: `F0 00 20 33 02 7F 43 00 00 01 F7`
- Stomp A einschalten: `F0 00 20 33 02 7F 01 00 32 03 00 01 F7`

Adressen immer mit der aktuellen Kemper "MIDI Parameter Documentation" abgleichen.

## Bedienkonzept der Oberfläche

- **Live-Ansicht** zum Spielen: Performance, großer Rig-Name, 9 Effekt-Kacheln, Footswitch-Leiste
- **Bearbeiten-Ansicht** zum Einstellen: zusätzlich Bereiche (Rig, Input, Amp, EQ, Cab, Output, System, Tuner), Morph, Tempo
- **Performance-Modus:** FS1–FS5 wählen die Slots, FS6 wechselt in den Stomp-Modus
- **Stomp-Modus:** FS1–FS5 frei belegbar (pro Rig, Performance oder global), auch als Morph (umschalten oder halten)

Alle Details zeigt der interaktive HTML-Prototyp im Ordner `prototype/`.
