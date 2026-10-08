# Upgrade-Hinweise

## Vor dem Update: Backup

```
mongodump --db structura_systems --out <backup-verzeichnis>
```

Beim ersten Start migriert der Server die Datenbank automatisch (Schema-Version 1 auf 2). Die Version steht in der Collection `schema`, Dokument `{_id: "schema", version: 2}`. Eine neu angelegte Datenbank erhaelt direkt Version 2.

## Was die Migration 1 -> 2 aendert

Die Migration ist idempotent und nutzt nur gezielte Updates, keine vollstaendigen Dokument-Ersetzungen. Alle Schritte werden mit dem Praefix `[migration]` auf stdout protokolliert.

1. Projekte mit `description == "Preloaded Project"` ohne `owner` und ohne `owningGroup` erhalten `isLibrary: true`.
2. Alle uebrigen Projekte ohne Eigentuemer werden mit `_id` und `name` als Warnung geloggt.
3. Benutzer: fehlt `role` oder ist sie nicht 0, 1, 2 oder 100, wird sie auf `0` gesetzt. Fehlt `group`, wird `""` gesetzt. Fehlt `mustChangePassword`, wird `false` gesetzt.
4. Jeder Benutzer, dessen Passwort `admin` lautet, erhaelt `mustChangePassword: true` und wird als Warnung geloggt.
5. Erst wenn alle Schritte erfolgreich waren, wird `schema.version = 2` geschrieben.

Schlaegt ein Schritt fehl, loggt der Server `[migration] FAILED: ...` und beendet sich mit Fehlercode. Ist die Datenbank neuer als das Binary (Version groesser 2), startet der Server ebenfalls nicht.

## Projekte ohne Eigentuemer

Projekte ohne `owner` (und keine Bibliotheksprojekte) werden mit Phase 2c nur noch fuer Administratoren sichtbar. Das Migrations-Log listet sie auf; ein Administrator kann sie anschliessend einem Benutzer oder einer Gruppe zuweisen.

## Standard-Administrator

Ein Konto, dessen Passwort noch `admin` lautet, bekommt `mustChangePassword: true`. Das Passwort sollte unmittelbar nach dem Update geaendert werden.

## Phase 2c: Rollenmodell und Zugriffskontrolle

Rollen: `USER` (0), `GROUP_MANAGER` (1), `GLOBAL_MODERATOR` (2), `GLOBAL_ADMINISTRATOR` (100). Jeder Benutzer hat genau eine Gruppe (`""` = keine).

Regeln:

- Bibliotheksprojekte sind nur fuer Administratoren sichtbar. Fuer alle anderen verhalten sie sich wie nicht vorhanden (404, nicht in `GET /projects`).
- Lesen und Schreiben eines Projekts: Administrator, Eigentuemer, Mitglieder der `owningGroup`. Loeschen und Eigentuemerwechsel: Eigentuemer oder Administrator. Unbekannte und verbotene Projekte liefern dieselbe 404-Antwort.
- Moderatoren haben ueber ihre Rolle keinen Projektzugriff. Sie verwalten nur Benutzer und Gruppen, koennen aber keine Moderator- oder Admin-Rolle vergeben und keine Admins/Moderatoren aendern oder loeschen.
- Gruppenmanager duerfen Benutzer ohne Gruppe (Rolle `USER`) in die eigene Gruppe aufnehmen und Mitglieder wieder entfernen (nur die Gruppe wird geleert, das Konto bleibt). Sie sehen unter `GET /admin/users` nur ihre Gruppe.
- Niemand loescht sich selbst. Der letzte Administrator kann weder geloescht (409) noch herabgestuft (409) werden.
- `POST /projects` ignoriert den Query-Parameter `owner`; Eigentuemer ist immer der angemeldete Benutzer. `owningGroup` (Body) darf nur eine eigene Gruppe sein (Admins: beliebig), sonst 403.

Neue Endpunkte:

- `PUT /projects/{id}/owner` mit Body `{"owner": "...", "owningGroup": "..."}` (beide optional)
- `GET /groups/{group}/members`, `POST /groups/{group}/members/{username}`, `DELETE /groups/{group}/members/{username}`
- `PUT /me/password` mit Body `{"oldPassword": "...", "newPassword": "..."}` (mindestens 12 Zeichen)
- `GET /me` liefert zusaetzlich `mustChangePassword`

Aenderungen an bestehenden Endpunkten:

- `PUT /admin/users/{username}` aendert jetzt den Benutzer (nur die uebergebenen Felder `password`, `group`, `role`). Bisher wurde der Benutzer faelschlich geloescht. Ein von einem anderen Benutzer gesetztes Passwort setzt `mustChangePassword`.
- `/admin/users` ist nicht mehr nur fuer Administratoren, sondern rollenabhaengig (siehe oben). Fehlende Rechte ergeben 403, ein doppelter Benutzername 409, ungueltige Eingaben 400 (Benutzername `^[A-Za-z0-9._-]{3,64}$`, Passwort ab 12 Zeichen, Rolle 0/1/2/100, Gruppe leer oder `^[A-Za-z0-9._-]{1,64}$`).
- Geloeschte Benutzer sowie Benutzer mit geaendertem Passwort oder geaenderter Rolle verlieren ihre Tokens sofort.
- Mit `mustChangePassword` antworten alle Endpunkte ausser `GET /me` und `PUT /me/password` mit 403 `{"error":"password_change_required"}`.

Projekte ohne Eigentuemer, die keine Bibliotheksprojekte sind, sind nur fuer Administratoren zugreifbar. Ein Administrator weist sie mit `PUT /projects/{id}/owner` einem Benutzer zu.

Initialer Administrator: Ist die Benutzertabelle leer, wird `admin` angelegt. Das Passwort stammt aus der Umgebungsvariable `STRUCTURA_ADMIN_PASSWORD`. Ist sie nicht gesetzt, erzeugt der Server ein zufaelliges Passwort mit 24 Zeichen, gibt es einmalig auf stdout aus und setzt `mustChangePassword`. Das feste `admin`/`admin` entfaellt.

## Phase 3: Sitzungen, Login-Schutz und Nebenlaeufigkeit

**Alle Benutzer muessen sich nach dem Update neu anmelden.** Sitzungen liegen nur im Speicher; bisherige Tokens sind nach dem Neustart ungueltig. Die neuen Tokens sind laenger (32 Zufallsbytes, URL-sicheres Base64 ohne Padding, 43 Zeichen). Clients, die das Token in einem festen Feld speichern, duerfen keine kuerzere Laenge annehmen.

Verhalten:

- Jeder Login erzeugt ein eigenes Token. Mehrere Anmeldungen desselben Benutzers teilen kein Token mehr.
- Ein Token verfaellt nach 1 Stunde ohne Nutzung und spaetestens 12 Stunden nach dem Login.
- `PUT /me/password` behaelt das Token der aufrufenden Sitzung, alle uebrigen Sitzungen des Benutzers werden ungueltig. Loeschen eines Benutzers sowie Passwort- oder Rollenaenderung durch einen Administrator beenden alle seine Sitzungen.
- `POST /login` ist begrenzt: hoechstens 5 fehlgeschlagene Anmeldungen pro Benutzername in 15 Minuten (ein erfolgreicher Login setzt den Zaehler nicht zurueck) und hoechstens 20 Anfragen pro Client-IP und Minute. Bei Ueberschreitung antwortet der Server mit 429, dem Header `Retry-After` (Sekunden) und `{"error":"too_many_requests"}`, auch bei richtigem Passwort.
- Hinter einem Reverse Proxy zaehlt die erste Adresse aus `X-Forwarded-For` nur dann als Client-IP, wenn die direkte Gegenstelle in `trusted_proxies` steht. Sonst gilt die Adresse der Verbindung.

Neue optionale Schluessel in `custom_config` (alle Werte positive ganze Zahlen, sonst gilt der Standard):

| Schluessel | Standard | Bedeutung |
|---|---|---|
| `session_idle_seconds` | 3600 | Leerlauf-Timeout einer Sitzung |
| `session_max_seconds` | 43200 | Maximale Lebensdauer einer Sitzung |
| `login_max_failures_per_user` | 5 | Fehlversuche pro Benutzername |
| `login_failure_window_seconds` | 900 | Zeitfenster dafuer |
| `login_max_requests_per_ip` | 20 | Login-Anfragen pro Client-IP |
| `login_ip_window_seconds` | 60 | Zeitfenster dafuer |

`trusted_proxies` (bereits seit Phase 0 gelesen, Standard `["127.0.0.1"]`) wird jetzt ausgewertet. Ist der Server direkt erreichbar, sollte die Liste leer sein, damit `X-Forwarded-For` nicht gefaelscht werden kann.

Intern: Die MongoDB-Verbindung nutzt jetzt einen Connection-Pool, die Services sind mit Lese-/Schreibsperren gegen parallele Zugriffe geschuetzt. Das erfordert keine Konfigurationsaenderung.

## Phase 4: CORS, Deployment und Konfiguration

Siehe [DEPLOYMENT.md](DEPLOYMENT.md).

- **CORS braucht `allowed_origins`.** Der Server spiegelt `Origin` nur noch zurueck, wenn er exakt in `allowed_origins` steht (auch in Debug-Builds), und sendet immer `Vary: Origin`. Fuer andere Origins gibt es keine CORS-Header. Wer bisher ohne Eintrag arbeitete (Debug: jede Origin erlaubt), muss die Web-Origin eintragen, sonst blockiert der Browser die Anfragen. Der alte Schluessel `allowed_origin` (String) wird weiter gelesen.
- **`config.json`** lauscht jetzt auf `127.0.0.1:8088` (statt `0.0.0.0`), loggt mit `INFO` und enthaelt keine MongoDB-Zugangsdaten mehr. Diese kommen aus `MONGO_USERNAME` und `MONGO_PASSWORD` oder aus einer eigenen Konfigurationsdatei (`STRUCTURA_CONFIG`). Der Server muss hinter einem Reverse Proxy laufen, der TLS terminiert.
- **`GET /version` braucht ein gueltiges Token** (401 ohne).
- Neu: `mongo_tls` / `MONGO_TLS=true`. Benutzer und Passwort der MongoDB werden Percent-kodiert.
- `docker-compose.yaml` liest Zugangsdaten aus einer `.env` (Vorlage `.env.example`), bindet Ports an `127.0.0.1` und startet mongo-express nur mit `--profile dev`.

## Schutz vor Datenverlust

Fehlt die Collection `versions`, obwohl andere Collections existieren, loescht der Server die Datenbank nicht mehr. Er bricht den Start mit einer Fehlermeldung ab. Nur in Debug-Builds und nur, wenn alle vorhandenen Collections leer sind, wird neu initialisiert.

## Rollback

Server stoppen, das alte Binary einsetzen und das Backup einspielen:

```
mongorestore --drop <backup-verzeichnis>
```

## Konfiguration

Aus Phase 0 gelten bereits:

- `MONGO_DATABASE` (Umgebungsvariable) ueberschreibt `mongo_database` aus der Konfiguration.
- `STRUCTURA_CONFIG` (Umgebungsvariable) gibt den Pfad zur Konfigurationsdatei an (Standard: `config.json`).

Weitere neue Config-Schluessel und Umgebungsvariablen folgen in spaeteren Phasen und werden hier ergaenzt.
