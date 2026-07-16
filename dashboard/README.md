# BME680 Live Dashboard

Ein kleines, selbst-gehostetes Web-Dashboard, das die Live-Werte des BME680
(Temperatur, Luftfeuchte, Luftdruck, Gas-Widerstand, IAQ) aus der Debug-Probe-UART
im Browser anzeigt.

```
dashboard/
├── server.py         Serial-Reader + HTTP-Server (nur Python-stdlib + pyserial)
├── index.html        Frontend (Kacheln, Sparklines, theme-aware)
├── dashboardctl.sh   Start/Stop des Hintergrundservers
└── README.md         diese Datei
```

## Hintergrundserver aktivieren / deaktivieren

Alles läuft über `dashboardctl.sh` (aus dem `dashboard/`-Verzeichnis):

```bash
cd dashboard

./dashboardctl.sh start     # Server im Hintergrund starten (überlebt Terminal-Schließen)
./dashboardctl.sh status    # läuft er? auf welchem Port?
./dashboardctl.sh stop      # Server beenden (gibt den Serial-Port wieder frei)
./dashboardctl.sh restart   # stop + start
./dashboardctl.sh logs      # Server-Log live mitlesen (Strg-C zum Beenden)
```

Nach `start` erreichbar unter **http://127.0.0.1:8080**.

- Der Server läuft via `nohup` losgelöst weiter, bis du `stop` aufrufst — auch
  nach dem Schließen des Terminals oder der Claude-Session.
- Die PID liegt in `dashboard/.server.pid`, das Log in `dashboard/.server.log`
  (beide gitignored).

### Manuell (im Vordergrund, ohne Control-Skript)

```bash
uv run --with pyserial python dashboard/server.py     # Strg-C beendet ihn
```

## ⚠️ Wichtig: nur ein Serial-Zugriff gleichzeitig

Der Serial-Port kann immer nur von **einem** Prozess gehalten werden. Solange der
Dashboard-Server läuft, kann `test/harness.py` den Port **nicht** öffnen (und
umgekehrt). Vor einem Harness-Lauf also erst:

```bash
./dashboardctl.sh stop
```

## Konfiguration (Environment-Variablen)

Werden sowohl von `server.py` als auch von `dashboardctl.sh` beachtet:

| Variable            | Default                        | Bedeutung                          |
|---------------------|--------------------------------|------------------------------------|
| `BME680_PORT`       | auto (`/dev/cu.usbmodem*`)     | Serieller Port                     |
| `BME680_BAUD`       | `115200`                       | Baudrate                           |
| `BME680_HTTP_HOST`  | `127.0.0.1`                    | Bind-Adresse (localhost)           |
| `BME680_HTTP_PORT`  | `8080`                         | HTTP-Port des Dashboards           |

Beispiel — festen Port erzwingen und auf 9090 lauschen:

```bash
BME680_PORT=/dev/cu.usbmodem4402 BME680_HTTP_PORT=9090 ./dashboardctl.sh start
```

Der Port wird sonst automatisch erkannt (erster passender `/dev/cu.usbmodem*`),
sodass ein Umstecken der Debug-Probe (z. B. `…4401` ↔ `…4402`) meist kein
Anpassen erfordert.

## Wie es funktioniert

1. Ein Hintergrund-Thread öffnet die UART (115200 Baud) und liest zeilenweise.
2. `[METRIC]`- und `[SUMMARY]`-Zeilen der Firmware werden geparst und in einen
   Ring-Puffer (letzte ~300 Messungen) geschrieben.
3. Der HTTP-Server liefert `/` (die Seite) und `/api/data` (JSON-Snapshot).
4. Das Frontend pollt `/api/data` alle 2 s und zeichnet Kacheln + Sparklines.
5. Fällt die Probe aus, versucht der Reader alle 2 s automatisch neu zu
   verbinden; die Seite zeigt dann „keine Serial-Daten".
