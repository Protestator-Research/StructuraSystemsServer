# Upgrade-Hinweise

## Vor dem Update: Backup

```
mongodump --db structura_systems --out <backup-verzeichnis>
```

Beim ersten Start migriert der Server die Datenbank automatisch (Schema-Version 1 auf 2 und 2 auf 3). Die Version steht in der Collection `schema`, Dokument `{_id: "schema", version: 3}`. Eine neu angelegte Datenbank erhaelt direkt Version 3. Die Aenderungen von Version 3 stehen im Abschnitt "Konformitaet zur Systems Modeling API (REST/HTTP-PSM)" weiter unten.

## Was die Migration 1 -> 2 aendert

Die Migration ist idempotent und nutzt nur gezielte Updates, keine vollstaendigen Dokument-Ersetzungen. Alle Schritte werden mit dem Praefix `[migration]` auf stdout protokolliert.

1. Projekte mit `description == "Preloaded Project"` ohne `owner` und ohne `owningGroup` erhalten `isLibrary: true`.
2. Alle uebrigen Projekte ohne Eigentuemer werden mit `_id` und `name` als Warnung geloggt.
3. Benutzer: fehlt `role` oder ist sie nicht 0, 1, 2 oder 100, wird sie auf `0` gesetzt. Fehlt `group`, wird `""` gesetzt. Fehlt `mustChangePassword`, wird `false` gesetzt.
4. Jeder Benutzer, dessen Passwort `admin` lautet, erhaelt `mustChangePassword: true` und wird als Warnung geloggt.
5. Erst wenn alle Schritte erfolgreich waren, wird `schema.version = 2` geschrieben.

Schlaegt ein Schritt fehl, loggt der Server `[migration] FAILED: ...` und beendet sich mit Fehlercode. Ist die Datenbank neuer als das Binary (Version groesser 3), startet der Server ebenfalls nicht.

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

## Konformitaet zur Systems Modeling API (REST/HTTP-PSM)

Der Server setzt jetzt die REST/HTTP-PSM der *Systems Modeling API and Services v1.1* um (Ausnahme: Derived Property Conformance). Das aendert das Verhalten bestehender Endpunkte. **Clients, vor allem das Webinterface, muessen angepasst werden.** Die Konformitaet belegen die Tests in `tests/integration/conformance/` (je Annex-A-Testfall eine Testfunktion, Name mit der ID, z. B. `test_pim_pcb_010_create_commit`).

### Breaking Changes

1. **Anlegende POSTs liefern 201** statt 200: `POST /projects/{id}/commits`, `POST …/branches`, `POST …/tags` und `POST …/queries`. Clients, die auf 200 pruefen, muessen 201 akzeptieren.
2. **Kein Erstcommit mehr.** Ein neues Projekt hat den Branch `main` mit `head = null` und noch keine Commits (`GET …/commits` liefert `[]`). Clients duerfen nicht mehr den letzten Commit eines neuen Projekts voraussetzen und muessen einen Branch ohne Head anzeigen koennen. Bibliotheksprojekte behalten ihren Import-Commit; ihr Default-Branch heisst bei neu angelegten Datenbanken `main` (bestehende Datenbanken behalten `Main`).
3. **Commits sind Deltas.** `change` enthaelt nur noch, was neu angelegt, geaendert oder geloescht wird, nicht mehr den Vollstand. Der Stand an einem Commit ist die Vereinigung der Staende seiner Vorgaenger plus der Aenderungen (`versionedData`, Kap. 7.1.2). `GET …/elements` liefert diesen kumulierten Stand. **Loeschungen** sendet der Client als `DataVersion` ohne `payload`: `{"@type": "DataVersion", "identity": {"@id": "<id>"}, "payload": null}`. Das Loeschen eines Elements loescht automatisch die Relationships, die es als `source` oder `target` haben. Geloeschte Daten bleiben in frueheren Commits sichtbar. Eine Loeschung ohne `identity` oder einer unbekannten Identitaet, dieselbe Identitaet zweimal in einem Commit und ein unbekannter Vorgaenger-Commit sind Fehler (400/404).
4. **`DataVersion` hat immer eine eigene `@id`** (zufaellige UUID, getrennt von der Id des Elements) und verweist mit `commit` auf ihren Commit. Die Id einer Aenderung (`GET …/changes/{changeId}`) ist diese `DataVersion`-Id.
5. **Branch-Auswahl beim Commit:** `POST …/commits?branchId=<uuid>` (ungueltige UUID 400, unbekannter oder fremder Branch 404). Der Alias `?branch=<Name>` bleibt, liefert bei unbekanntem Namen aber **404 statt des stillen Fallbacks auf den Default-Branch**. Ohne Parameter gilt der Default-Branch. `CommitRequest.previousCommits` wird ausgewertet (die Vorgaenger sind der Head des Branches plus die angegebenen Commits); ein unbekannter oder fremder Commit ergibt 404.
6. **`name` ist Pflicht** bei `POST /projects` (nicht leerer String, sonst 400) und bei `POST …/branches`.
7. **Unbekannter Payload-`@type` ergibt 400.** Erlaubt sind KerML/SysML-Elemente, `ProjectUsage`, `ExternalRelationship` und `ExternalData`. Bisher wurde ein unbekannter Typ als `{"@id","@type"}` gespeichert.
8. **Eine leere `change`-Liste ergibt 400.**
9. **Pagination fuer alle Collection-GETs.** Ohne Parameter liefern sie hoechstens 100 Eintraege (`page[size]`, 1 bis 1000, sonst 400). Weitere Seiten stehen im Header `Link` (`rel="next"` / `rel="prev"`, absolute URL) und werden mit `page[after]` / `page[before]` (undurchsichtiger Cursor, ungueltig 400) abgerufen. **Clients, die `GET /projects` oder andere Listen ohne Parameter lesen, bekommen bei mehr als 100 Eintraegen nur die erste Seite** und muessen dem `Link`-Header folgen. Betroffen: Projekte, Commits, Branches, Tags, Elements, Roots, Relationships, Changes, Diff, Queries, Query-Ergebnisse, Twins. Hinter einem Reverse Proxy werden `X-Forwarded-Proto` und `X-Forwarded-Host` fuer die Link-URLs nur beruecksichtigt, wenn die Gegenstelle in `trusted_proxies` steht.
10. **Fehlerantworten sind JSON** `{"error": "..."}`, auch 404 ohne Body und der Klartext bei unbekanntem Commit. Fremde Commits und Branches eines anderen Projekts ergeben 404 (wie unbekannte), damit die Zugriffskontrolle keine Aufzaehlung zulaesst.
11. **`/projects/{id}/commits/{c}/root` heisst jetzt `…/roots`** (der alte Pfad bleibt als Alias). `…/relationships` und `…/roots` waren bisher 501.

### Schema 3 und automatische Migration

`CURRENT_SCHEMA_VERSION` ist 3. Die Migration 2 -> 3 laeuft beim ersten Start automatisch, ist idempotent und schreibt die Version erst nach Erfolg:

1. Branches erhalten `owningProject` aus `_project_id` (bisher `null`).
2. In gespeicherten Commits erhaelt jede `DataVersion` ohne `@id` oder mit der Id des Payloads als `@id` eine zufaellige UUID. Fehlen `identity` oder `commit`, werden sie ergaenzt.

Bestehende Commits behalten ihren Inhalt. Weil sie bisher den Vollstand enthielten, gilt fuer sie weiterhin dieser Inhalt; Elemente, die ein altes Webinterface durch Weglassen "geloescht" hat, bleiben im kumulierten Stand sichtbar. Backup vor dem Update (siehe oben). Der Rollback geht nur ueber das Backup, weil die alte Version Schema 3 nicht liest.

### Neue Routen

| Route | Zweck |
|---|---|
| `PUT /projects/{id}` | Name, Beschreibung, Default-Branch aendern (`defaultBranch: {"@id"}`; unbekannter Branch 404) |
| `POST /projects/{id}/tags`, `DELETE …/tags/{tagId}` | Tags anlegen (201, doppelter Name 409) und loeschen |
| `GET …/commits/{c}/diff?baseCommitId=&changeTypes=` | Unterschiede zwischen zwei Commits (`DataDifference`) |
| `POST …/branches/{b}/merge?sourceCommitId=&description=` | Commits in einen Branch mergen; 409 mit den Konflikt-Identitaeten, Body optional als Resolution (Array von Data) |
| `GET …/changes?changeTypes=CREATED,UPDATED,DELETED` | Filter fuer die Aenderungen (Alias `change_type`) |
| `GET …/elements/{e}/relationships?direction=in\|out\|both` | Relationships eines Elements |
| `GET …/commits/{c}/roots` | Elemente ohne Owner |
| `GET/POST /projects/{p}/queries`, `GET/PUT/DELETE …/queries/{q}`, `GET …/queries/{q}/results`, `GET/POST /projects/{p}/query-results` | QueryService; Queries liegen in der neuen Collection `queries` und werden mit dem Projekt geloescht |

### Bibliothek

Der Server benoetigt `sysmllib/2610.3` (statt 2610.2). Die Versionierungslogik (`versionedData`, Commit-Pruefung, Diff, Merge, Query-Auswertung) liegt in der Bibliothek, Namespace `SysMLv2::REST::Versioning`. Nach dem Wechsel `conan install . -s build_type=Debug --build missing` ausfuehren.

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
