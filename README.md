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
.github/workflows/          # prüft, kompiliert und veröffentlicht den Webinstaller
web/                        # Installationsseite (ESP Web Tools) + Manifest
```

## Einrichtung (Windows)

1. **Git** installieren: <https://git-scm.com/download/win>
2. **Python** installieren (3.12 oder neuer): <https://www.python.org/downloads/> – beim Setup „Add python.exe to PATH“ anhaken.
3. **Repo klonen**, eine eigene virtuelle Umgebung anlegen und ESPHome darin installieren (PowerShell).
   ESPHome pinnt viele Pakete exakt (z. B. `click`) – in der globalen Python-Installation
   kollidiert das mit anderen Tools wie `huggingface-hub`. Deshalb immer in `.venv` arbeiten:
   ```powershell
   git clone https://github.com/jfpuetz/wassist.git
   cd wassist
   py -m venv .venv
   .\.venv\Scripts\Activate.ps1          # Prompt zeigt danach (.venv)
   python -m pip install --upgrade pip
   pip install -r requirements.txt
   esphome version
   code .
   ```
   Falls `Activate.ps1` wegen der Ausführungsrichtlinie blockiert wird, einmalig:
   `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
   In jedem neuen Terminal vor der Arbeit erneut `.\.venv\Scripts\Activate.ps1` ausführen.
   VS Code erkennt `.venv` automatisch (unten rechts als Python-Interpreter auswählen);
   das integrierte Terminal aktiviert sie dann selbst.
4. **VS-Code-Erweiterungen:**
   VS Code schlägt die empfohlenen Erweiterungen vor (ESPHome, YAML, C++, GitLens) → installieren.
5. **USB-Treiber:** Der „USB“-Port des DevKit (native USB) braucht unter Windows 10/11 keinen Treiber.
   Der „UART“/„COM“-Port nutzt je nach Revision einen CP210x- oder CH343-Chip – falls kein COM-Port erscheint, den passenden Treiber installieren.

## Flashen über den Browser (ohne Installation)

Wie bei WLED: **<https://jfpuetz.github.io/wassist/>** in Chrome oder Edge öffnen →
„Firmware installieren“ → Port wählen. Direkt danach fragt die Seite das WLAN ab.
Die Seite wird bei jedem Push auf `main` von GitHub Actions neu gebaut und zeigt immer die aktuelle Firmware.
Die fertigen `.bin`-Dateien liegen außerdem bei jedem Actions-Lauf als Artefakt zum Download.

## Erstes Flashen (lokal, für Entwicklung)

> ⚠️ **Nie PC-USB und USB-PD-Netzteil gleichzeitig anstecken.**
> USB-Port und 5V-Pin des DevKit dürfen laut Espressif nicht gleichzeitig speisen (-U8 liegt am 5V-Pin).
> Zum Flashen das PD-Netzteil abziehen, dann reicht das USB-Kabel allein – nichts abklemmen nötig.
> Ohne PD-Netzteil meldet der HUSB238 „nicht verbunden“ und Lautsprecher/LEDs bleiben aus (normal).
> Dauerhafte Lösung: Schottky-Diode (1N5819/SS34) zwischen -U8-Ausgang und 5V-Pin, -U8 dann auf ~5,3 V.

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

## WLAN einrichten (ohne secrets.yaml, wie bei WLED)

Die Firmware enthält keine Zugangsdaten. Nach dem ersten Flashen:

1. Der Node öffnet den Hotspot **„WAssist Node 1 Setup“** (offen, ohne Passwort).
2. Mit dem Handy verbinden – die Einrichtungsseite öffnet sich automatisch, sonst <http://192.168.4.1>.
3. Heim-WLAN auswählen, Passwort eingeben, speichern. Der Node startet neu und verbindet sich.

Alternativ direkt nach dem Flashen per USB: <https://web.esphome.io> in Chrome/Edge öffnen →
„Connect“ → „Configure Wi-Fi“ (Improv Serial).

Findet der Node sein gespeichertes WLAN später 30 s lang nicht, öffnet er den Hotspot erneut.

## In Home Assistant einbinden

Einstellungen → Geräte & Dienste → der Node erscheint unter „Entdeckt“ als ESPHome-Gerät → Hinzufügen.
Home Assistant erzeugt dabei selbst den API-Schlüssel und überträgt ihn auf den Node; ab dann
sind nur noch verschlüsselte Verbindungen möglich. Kein Schlüssel muss abgetippt werden.

Neu einrichten (anderes WLAN, anderes HA): Button **„Werkseinstellungen“** am Gerät in HA –
löscht WLAN-Daten und API-Schlüssel, danach erscheint wieder der Setup-Hotspot.

> **Hinweis OTA:** Updates per WLAN sind ohne Passwort möglich (jedes Gerät im selben Netz könnte flashen).
> Wer das absichern will, setzt in `esphome/packages/base.yaml` unter `ota:` ein `password:`.

Danach unter Einstellungen → Sprachassistenten eine Assist-Pipeline wählen. Wake Word: „Okay Nabu“.

## Offene Punkte (siehe Schaltplan-Notizen)

- [ ] INA219: LED-Strom < 3 A halten (aktuell per Software auf 40 % Helligkeit begrenzt) **oder** Shunt tauschen
- [ ] WS2812B-Pegel: -U7 auf 4,5 V **oder** 74AHCT125
- [ ] -U3 SD an einen GPIO → Software-Mute
- [ ] MP1584EN-Reserve für ESP32 + Endstufe prüfen
- [ ] HUSB238: Spannung per I2C wählen (aktuell nur lesend, Jumper bestimmt 9 V)

## Lizenz

Apache License 2.0 – siehe [LICENSE](LICENSE).
