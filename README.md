# WAssist

Selbstgebauter Sprachassistent-Node für Home Assistant auf Basis des ESP32-S3.
Firmware: [ESPHome](https://esphome.io), Wake Word läuft lokal auf dem Chip (micro_wake_word),
Spracherkennung und Antwort übernimmt Home Assistant Assist.

| Funktion | Bauteil | Anschluss |
|---|---|---|
| MCU | ESP32-S3-DevKitC-1 N16R8 (-U1) | – |
| Mikrofon | INMP441 (-U4) | I2S0: BCLK GPIO4, WS GPIO5, SD GPIO6 |
| Verstärker | MAX98357A (-U3) + 4-Ω-Lautsprecher | I2S1: BCLK GPIO15, LRC GPIO16, DIN GPIO17 |
| Status-LEDs | WS2812B-Streifen | GPIO38 über -R1 |
| USB-PD | HUSB238 (-U2), 0x08 | I2C GPIO8/9 |
| Strommessung | INA219 (-U5 0x40 9-V-Schiene, -U6 0x41 LED-Zweig) | I2C GPIO8/9 |

Verbindlicher Schaltplan: `WASSIST-N1-E01…E04`, Rev. 01.
> **Bitte prüfen:** Die I2C-Pins (SDA GPIO8, SCL GPIO9) sind aus der Liste freier GPIOs übernommen.
> Falls Blatt 3 andere Pins nennt, in `esphome/wassist-node1.yaml` unter `substitutions` anpassen.

## Aufbau des Repos

```
esphome/
  wassist-node1.yaml        # Einstieg: Pins, Namen, Grenzwerte (substitutions)
  packages/
    base.yaml               # Board, PSRAM, WLAN, API, OTA, I2C
    power.yaml              # HUSB238 + INA219
    audio.yaml              # I2S-Mikrofon und -Verstärker
    voice.yaml              # Wake Word + Voice Assistant
    led.yaml                # LED-Streifen + Statusfarben
  components/husb238/       # eigene ESPHome-Komponente (C++), liest PD-Status
  secrets.yaml.example      # Vorlage für WLAN/API-Schlüssel
.github/workflows/          # prüft und kompiliert bei jedem Push
```

## Einrichtung (Windows)

1. **Git** installieren: <https://git-scm.com/download/win>
2. **Python 3.12** installieren: <https://www.python.org/downloads/> – beim Setup „Add python.exe to PATH“ anhaken.
3. **Repo klonen**, ESPHome in der festgelegten Version installieren und VS Code öffnen (PowerShell):
   ```powershell
   git clone https://github.com/jfpuetz/wassist.git
   cd wassist
   py -m pip install --upgrade pip
   py -m pip install -r requirements.txt
   esphome version
   code .
   ```
4. **VS-Code-Erweiterungen:**
   VS Code schlägt die empfohlenen Erweiterungen vor (ESPHome, YAML, C++, GitLens) → installieren.
5. **Secrets anlegen:** `esphome/secrets.yaml.example` nach `esphome/secrets.yaml` kopieren und ausfüllen.
   API-Schlüssel erzeugen:
   ```powershell
   py -c "import secrets,base64;print(base64.b64encode(secrets.token_bytes(32)).decode())"
   ```
6. **USB-Treiber:** Der „USB“-Port des DevKit (native USB) braucht unter Windows 10/11 keinen Treiber.
   Der „UART“/„COM“-Port nutzt je nach Revision einen CP210x- oder CH343-Chip – falls kein COM-Port erscheint, den passenden Treiber installieren.

## Erstes Flashen

> ⚠️ **Vor dem Anstecken per USB `-U8` (MP1584EN, 5-V-Logik) trennen.**
> USB, 5V-Pin und 3V3-Pin des DevKit dürfen laut Espressif nicht gleichzeitig speisen.

```powershell
cd wassist
esphome config esphome/wassist-node1.yaml   # nur prüfen
esphome run esphome/wassist-node1.yaml      # kompilieren + flashen + Log anzeigen
```

Der erste Build dauert 5–15 Minuten (Toolchain wird geladen). Danach geht jedes weitere Update
per WLAN (OTA): `esphome run …` bietet dann die IP des Nodes an.

Im Log nach dem Start prüfen:
- `i2c` findet Geräte bei **0x08, 0x40, 0x41**
- `husb238`: „Source offers 9 V @ … A“ und „Contract: 9 V / 3.00 A“
- keine Fehlermeldungen von `i2s_audio`

## In Home Assistant einbinden

Einstellungen → Geräte & Dienste → der Node erscheint als „ESPHome“ → Hinzufügen → API-Schlüssel aus `secrets.yaml` eingeben.
Danach unter Einstellungen → Sprachassistenten eine Assist-Pipeline wählen. Wake Word: „Okay Nabu“.

## Offene Punkte (siehe Schaltplan-Notizen)

- [ ] INA219: LED-Strom < 3 A halten (aktuell per Software auf 40 % Helligkeit begrenzt) **oder** Shunt tauschen
- [ ] WS2812B-Pegel: -U7 auf 4,5 V **oder** 74AHCT125
- [ ] -U3 SD an einen GPIO → Software-Mute
- [ ] MP1584EN-Reserve für ESP32 + Endstufe prüfen
- [ ] HUSB238: Spannung per I2C wählen (aktuell nur lesend, Jumper bestimmt 9 V)

## Lizenz

Apache License 2.0 – siehe [LICENSE](LICENSE).
