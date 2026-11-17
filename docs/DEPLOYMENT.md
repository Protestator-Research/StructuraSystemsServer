# Deployment

## Architektur

```
Browser / Client --HTTPS--> Reverse Proxy (TLS) --HTTP--> Server 127.0.0.1:8088 --> MongoDB (localhost:27017)
```

Der Server spricht kein TLS. Er lauscht nur auf `127.0.0.1` und muss hinter einem Reverse Proxy laufen, der TLS terminiert. MongoDB und der Server duerfen nicht aus dem Netz erreichbar sein.

## Reverse Proxy

Caddy (setzt `X-Forwarded-For` von selbst):

```
structura.example.com {
    reverse_proxy 127.0.0.1:8088
}
```

nginx:

```
location / {
    proxy_pass http://127.0.0.1:8088;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $remote_addr;
}
```

`trusted_proxies` muss die IP enthalten, von der der Proxy den Server erreicht (bei lokalem Proxy `127.0.0.1`, das ist der Standard). Der Server wertet `X-Forwarded-For` nur aus, wenn die direkte Gegenstelle in dieser Liste steht. Der Grund:

- Ohne die Liste saehe der Server fuer alle Clients die Proxy-IP, und die Begrenzung der Login-Anfragen pro IP wuerde alle Benutzer gemeinsam treffen.
- Steht zu viel in der Liste (oder ist der Server direkt erreichbar), kann ein Angreifer `X-Forwarded-For` faelschen und das IP-Limit umgehen. Der Proxy muss den Header deshalb setzen und nicht nur durchreichen (`$remote_addr` statt `$proxy_add_x_forwarded_for`).

Ist der Server ohne Proxy direkt erreichbar, setzen Sie `"trusted_proxies": []`.

## Umgebungsvariablen

| Variable | Bedeutung |
|---|---|
| `STRUCTURA_CONFIG` | Pfad der Konfigurationsdatei (Standard `config.json` im Arbeitsverzeichnis) |
| `STRUCTURA_ADMIN_PASSWORD` | Passwort des ersten Administrators, nur wirksam bei leerer Benutzertabelle. Ohne Angabe erzeugt der Server ein Zufallspasswort und gibt es einmalig auf stdout aus |
| `MONGO_ADDRESS` | ueberschreibt `mongo_address` |
| `MONGO_USERNAME` | ueberschreibt `mongo_username` |
| `MONGO_PASSWORD` | ueberschreibt `mongo_password` |
| `MONGO_DATABASE` | ueberschreibt `mongo_database` |
| `MONGO_TLS` | `true` aktiviert TLS zur Datenbank (zusaetzlich zu `mongo_tls`) |

Benutzername und Passwort werden fuer die Verbindung automatisch Percent-kodiert, Sonderzeichen wie `@ : / % #` sind erlaubt. Die Anmeldung erfolgt gegen die Datenbank `admin`.

## Schluessel in `custom_config`

Eine kommentierte Vorlage ist `config.example.json`.

| Schluessel | Standard | Bedeutung |
|---|---|---|
| `mongo_address` | `localhost:27017` | Host und Port von MongoDB |
| `mongo_username` | leer | Benutzer, besser ueber `MONGO_USERNAME` |
| `mongo_password` | leer | Passwort, besser ueber `MONGO_PASSWORD` |
| `mongo_database` | `structura_systems` | Datenbankname |
| `mongo_tls` | `false` | TLS zur Datenbank |
| `allowed_origins` | `[]` | Erlaubte Browser-Origins (CORS), exakter Vergleich, z. B. `["https://app.example.com"]`. Leer: keine CORS-Header. Der alte Schluessel `allowed_origin` (String) wird weiter gelesen |
| `trusted_proxies` | `["127.0.0.1"]` | Proxys, deren `X-Forwarded-For` gilt |
| `session_idle_seconds` | 3600 | Leerlauf-Timeout einer Sitzung |
| `session_max_seconds` | 43200 | Maximale Lebensdauer einer Sitzung |
| `login_max_failures_per_user` | 5 | Fehlversuche pro Benutzername |
| `login_failure_window_seconds` | 900 | Zeitfenster dafuer |
| `login_max_requests_per_ip` | 20 | Login-Anfragen pro Client-IP |
| `login_ip_window_seconds` | 60 | Zeitfenster dafuer |
| `api_token_default_lifetime_days` | 90 | Laufzeit eines API-Tokens, wenn die Anfrage `expiresInDays` nicht angibt |
| `api_token_max_lifetime_days` | 365 | Obergrenze fuer `expiresInDays`; hoehere Werte werden auf 365 begrenzt |
| `api_token_max_count` | 200 | Hoechstzahl gleichzeitig vorhandener API-Tokens (danach 409) |

Alle Werte sind positive ganze Zahlen, sonst gilt der Standard. Die Standardlaufzeit wird auf die Obergrenze begrenzt. Ein weiterer Schluessel, `api_token_lifetime_override_seconds`, ist **nur fuer Tests** gedacht (er kann die Laufzeit neuer Tokens nur verkuerzen: sie betraegt hoechstens diese Sekundenzahl) und darf in Produktion nicht gesetzt werden; der Server warnt beim Start, wenn er gesetzt ist.

Die Konfiguration `config.json` im Repository enthaelt keine Zugangsdaten und lauscht auf `127.0.0.1:8088`.

## API-Tokens im Betrieb

Beschreibung der Endpunkte und Rechte: [UPGRADE.md](UPGRADE.md), Abschnitt "API-Tokens".

- Das Token gehoert nur in den `Authorization`-Header (`Bearer sst_...`), **nie in eine URL** oder einen Query-Parameter, weil URLs in den Logs von Reverse Proxy und Client landen. Der Reverse Proxy sollte den `Authorization`-Header nicht protokollieren.
- Der Klartext wird bei der Ausstellung genau einmal angezeigt und ist danach nicht wiederherstellbar; der Server speichert nur den SHA-256-Hash. Ein verlorenes Token wird widerrufen und neu ausgestellt. Der Klartext steht nie im Server-Log.
- **Server-Tokens duerfen alles lesen, schreiben und loeschen, auch Bibliotheksprojekte** (mit `read_write`; mit `read` alles lesen). Sie sparsam ausstellen, nur mit `read`, wo das reicht, und mit kurzer Laufzeit. Projekt- und Gruppen-Tokens sind vorzuziehen.
- **Beim Ausscheiden eines Administrators die Tokens pruefen.** Tokens sind nicht an das Konto des Ausstellers gebunden und bleiben nach dessen Loeschen oder Herabstufen gueltig. `GET /admin/tokens` zeigt `createdBy` fuer jedes Token; unerwuenschte Tokens mit `DELETE /admin/tokens/{id}` widerrufen. Spaetestens nach 365 Tagen laufen alle Tokens ab.
- Ein Widerruf wirkt fuer jede neue Anfrage sofort. Eine Anfrage, die bereits bearbeitet wird, unterbricht er nicht.
- `lastUsedAt` wird im Speicher gefuehrt und hoechstens alle 5 Minuten (und beim geordneten Beenden des Servers) in die Datenbank geschrieben. Der angezeigte Wert ist im laufenden Betrieb aktuell, nach einem Absturz kann er bis zu 5 Minuten alt sein.
- Ausstellen und Widerruf erscheinen als Zeilen mit `[audit]` auf stdout (Token-Id, Name, Ziel, Zugriffsart, Aussteller). Zum Auswerten die Ausgabe des Dienstes sammeln.
- **Eine Serverinstanz je Datenbank.** Tokens liegen beim Start im Speicher des Prozesses. Widerruf und neue Tokens wirken nur im laufenden Prozess; eine zweite Instanz auf derselben Datenbank sieht sie erst nach ihrem Neustart.
- Der Widerruf wirkt im laufenden Prozess sofort, auch wenn MongoDB nicht erreichbar ist. Schlaegt das Loeschen in der Datenbank fehl, antwortet `DELETE` mit 503, der Server protokolliert die Token-Id und wiederholt das Loeschen (Housekeeping, geordnetes Beenden, erneutes `DELETE`). Wird der Prozess vorher beendet, bleibt das Dokument bestehen und das Token gilt nach dem Neustart wieder; dann erneut widerrufen.
- Abgelaufene Tokens bleiben 30 Tage erhalten und sind mit `expired: true` sichtbar, damit eine falsch gehende Systemuhr keine Tokens loescht. Ihr Name darf wiederverwendet werden, sie zaehlen nicht zur Hoechstzahl.
- Das Ausstellen eines Tokens und das Loeschen eines Projekts halten den Speicher des Token-Stores waehrend ihrer Datenbankzugriffe gesperrt; bei langsamer MongoDB warten in dieser Zeit auch die Authentifizierungen mit API-Tokens.
- Ein Server-Token mit `read_write` kann alle Projekte loeschen, auch Bibliotheksprojekte. Seine Nutzung wird nicht je Anfrage protokolliert, nur `lastUsedAt` wird gefuehrt.
- Gruppen-Tokens folgen `owningGroup`: Wer Projekte in die Gruppe oder aus ihr heraus verschiebt, aendert, was das Token abdeckt.

## Lokal starten

Mit Umgebungsvariablen (Docker-Compose-MongoDB aus diesem Repository):

```
MONGO_USERNAME=root MONGO_PASSWORD=<root-passwort> ./StructuraSystemsServer
```

Oder mit einer lokalen, nicht eingecheckten Datei `config.local.json` (steht in `.gitignore`), die Sie aus `config.example.json` kopieren:

```
STRUCTURA_CONFIG=config.local.json ./StructuraSystemsServer
```

## Laufzeitbibliotheken

Das Binary haengt von den Conan-Bibliotheken ab (Boost, Drogon, mongocxx, ...). Entweder laden Sie vor dem Start die Laufzeitumgebung oder installieren die Bibliotheken ins System:

```
source build/<Build-Typ>/generators/conanrun.sh
./build/<Build-Typ>/debug/StructuraSystemsServer
```

## MongoDB mit Docker Compose

```
cp .env.example .env     # Werte ersetzen
docker compose up -d     # nur MongoDB
docker compose --profile dev up -d   # zusaetzlich mongo-express (nur Entwicklung)
```

Die Ports sind an `127.0.0.1` gebunden. Beim ersten Start einer **leeren** Daten-Volume legt `docker/mongo-init.js` den Anwendungsbenutzer (`MONGO_APP_USERNAME`) mit `readWrite` ausschliesslich auf `structura_systems` an. Der Server sollte mit diesem Benutzer statt mit `root` laufen.

Init-Skripte laufen nur bei leerem Datenverzeichnis, ebenso werden `MONGO_INITDB_ROOT_*` bei einer bestehenden Volume ignoriert. Bei bestehenden Installationen legen Sie den Benutzer von Hand an (als root):

```
mongosh -u <root> -p --authenticationDatabase admin --eval '
  db.getSiblingDB("admin").createUser({
    user: "structura", pwd: passwordPrompt(),
    roles: [{ role: "readWrite", db: "structura_systems" }]
  })'
```

Setzen Sie in der `.env` bei einer bestehenden Volume fuer `MONGO_ROOT_*` die Werte, die in der Datenbank tatsaechlich gelten, bis Sie das root-Passwort in MongoDB geaendert haben (`db.changeUserPassword("root", ...)` in `admin`).

## Zugangsdaten rotieren

**Alle Zugangsdaten, die jemals eingecheckt waren, gelten als kompromittiert und muessen ersetzt werden.** Sie stehen in der Git-History:

- MongoDB-root-Passwort `root-password`
- mongo-express-Zugang `mongoexpresspass`
- jedes Konto `admin`/`admin` der Anwendung (Passwort ueber `PUT /me/password` aendern)

Das Entfernen aus dem aktuellen Stand reicht nicht.

## Entwicklung

Integrationstests: `tests/integration/run.sh` (benoetigt eine laufende MongoDB; Zugang ueber `TEST_MONGO_USER`/`TEST_MONGO_PASSWORD` oder `TEST_MONGO_URI`, Binary ueber `STRUCTURA_SERVER_BINARY`, Laufzeitumgebung ueber `STRUCTURA_CONAN_RUN_ENV`).

AddressSanitizer/UBSan-Build (nach `conan install` fuer Debug):

```
cmake --preset debug-asan
cmake --build build/asan -j8
STRUCTURA_SERVER_BINARY=$PWD/build/asan/debug/StructuraSystemsServer tests/integration/run.sh
```

Ohne Preset:

```
cmake -S . -B build/asan -DCMAKE_TOOLCHAIN_FILE=$PWD/build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Siehe auch [UPGRADE.md](UPGRADE.md).
