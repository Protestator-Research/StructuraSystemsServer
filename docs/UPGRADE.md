# Upgrade-Hinweise

## Vor dem Update: Backup

```
mongodump --db structura_systems --out <backup-verzeichnis>
```

Beim ersten Start migriert der Server die Datenbank automatisch (Schema-Version 1 auf 2, 2 auf 3 und 3 auf 4). Die Version steht in der Collection `schema`, Dokument `{_id: "schema", version: 4}`. Eine neu angelegte Datenbank erhaelt direkt Version 4. Die Aenderungen von Version 3 stehen im Abschnitt "Konformitaet zur Systems Modeling API (REST/HTTP-PSM)" weiter unten, die von Version 4 im Abschnitt "API-Tokens".

## Was die Migration 1 -> 2 aendert

Die Migration ist idempotent und nutzt nur gezielte Updates, keine vollstaendigen Dokument-Ersetzungen. Alle Schritte werden mit dem Praefix `[migration]` auf stdout protokolliert.

1. Projekte mit `description == "Preloaded Project"` ohne `owner` und ohne `owningGroup` erhalten `isLibrary: true`.
2. Alle uebrigen Projekte ohne Eigentuemer werden mit `_id` und `name` als Warnung geloggt.
3. Benutzer: fehlt `role` oder ist sie nicht 0, 1, 2 oder 100, wird sie auf `0` gesetzt. Fehlt `group`, wird `""` gesetzt. Fehlt `mustChangePassword`, wird `false` gesetzt.
4. Jeder Benutzer, dessen Passwort `admin` lautet, erhaelt `mustChangePassword: true` und wird als Warnung geloggt.
5. Erst wenn alle Schritte erfolgreich waren, wird `schema.version = 2` geschrieben.

Schlaegt ein Schritt fehl, loggt der Server `[migration] FAILED: ...` und beendet sich mit Fehlercode. Ist die Datenbank neuer als das Binary (Version groesser 4), startet der Server ebenfalls nicht.

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

## API-Tokens

Anwendungen (Skripte, CI-Jobs, Werkzeuge) melden sich mit einem eigenen, einzeln widerrufbaren Token an der API an, ohne Benutzerkonto und Passwort: `Authorization: Bearer sst_...`. Ein Administrator stellt das Token fuer genau ein Ziel aus. Das Token ist eine eigene Identitaet und kein Stellvertreter eines Benutzers; seine Rechte ergeben sich aus Ziel und Zugriffsart. Der Entwurf steht in [API_TOKEN_PLAN.md](API_TOKEN_PLAN.md).

### Schema 4 und Migration

`CURRENT_SCHEMA_VERSION` ist 4. Die Migration 3 -> 4 laeuft beim ersten Start automatisch, ist idempotent und schreibt die Version erst nach Erfolg:

- Die neue Collection `api_tokens` wird angelegt, mit eindeutigen Indizes auf `tokenHash` und auf `name`.
- Bestehende Daten (Projekte, Benutzer, Commits, ...) werden nicht veraendert. Eine neue Datenbank erhaelt Collection und Indizes direkt.
- Gespeichert wird nur der SHA-256-Hash des Tokens, nie der Klartext.

Rollback auf ein Binary mit Schema 3: Das alte Binary startet nicht, solange `schema.version` 4 ist. Weil nur eine Collection hinzukommt, genuegt es, die Version zurueckzusetzen (`db.schema.updateOne({_id: "schema"}, {$set: {version: 3}})`); die Collection `api_tokens` wird vom alten Binary ignoriert und kann bei Bedarf geloescht werden. Solange das alte Binary laeuft, sind die Tokens ungueltig. **Bleibt `api_tokens` erhalten, werden beim naechsten Upgrade auf Schema 4 alle noch nicht abgelaufenen Tokens wieder gueltig.** Sollen sie ungueltig bleiben, beim Rollback die Collection `api_tokens` loeschen (`db.api_tokens.drop()`). Sicherer bleibt der Weg ueber das Backup (siehe "Rollback").

### Endpunkte

Alle Endpunkte sind nur mit der Login-Sitzung eines `GLOBAL_ADMINISTRATOR` erreichbar. Andere Rollen erhalten 403 `{"error":"Insufficient permissions."}`, ein API-Token 403 `{"error":"session_required"}`, ein Administrator mit offenem Passwortwechsel 403 `{"error":"password_change_required"}`.

| Methode | Pfad | Antwort |
|---|---|---|
| `POST` | `/admin/tokens` | 201 mit den Metadaten und einmalig `token`; 400 ungueltige Eingabe oder unbekanntes Projekt (auch Bibliotheksprojekte), 409 Name vergeben oder Hoechstzahl erreicht (abgelaufene Tokens zaehlen nicht zur Hoechstzahl; der Name eines abgelaufenen Tokens darf wiederverwendet werden) |
| `GET` | `/admin/tokens` | alle Tokens, auch abgelaufene (paginiert wie die uebrigen Listen), ohne Klartext und ohne Hash |
| `GET` | `/admin/tokens/{id}` | ein Token, 404 wenn unbekannt oder keine gueltige UUID |
| `DELETE` | `/admin/tokens/{id}` | 204, der Widerruf wirkt sofort; 404 wenn unbekannt oder keine gueltige UUID; 503 `{"error":"Token revoked for this server process; the database delete failed and will be retried."}`, wenn das Token zwar in diesem Serverprozess sofort ungueltig ist, das Loeschen in MongoDB aber fehlschlug (es wird wiederholt, ein erneutes `DELETE` versucht es ebenfalls) |

Anfrage:

```
POST /admin/tokens
{
  "name": "ci-importer",
  "target": { "type": "project", "projects": ["<uuid>", "<uuid>"] },
  "scope": "read_write",
  "expiresInDays": 90
}
```

- `name`: Pflicht, serverweit eindeutig, `^[A-Za-z0-9 ._-]{1,64}$`.
- `target.type`: Pflicht, `project`, `group` oder `server`. `projects` (1 bis 50 vorhandene, keine Bibliotheksprojekte, ohne Duplikate) nur und zwingend bei `project`; `group` (`^[A-Za-z0-9._-]{1,64}$`) nur und zwingend bei `group`; bei `server` keine weiteren Felder.
- `scope`: Pflicht, `read` oder `read_write` (kein Standardwert).
- `expiresInDays`: optional, ganze Zahl von 1 bis `api_token_max_lifetime_days` (hoechstens 365; Standard 90, siehe Konfiguration), sonst 400. Tokens laufen immer ab.

Antwort (201), die Antwort traegt `Cache-Control: no-store`:

```
{
  "id": "3f0c8d2e-...",
  "name": "ci-importer",
  "target": { "type": "project", "projects": ["<uuid>", "<uuid>"] },
  "scope": "read_write",
  "createdBy": "admin",
  "createdAt": 1767000000,
  "expiresAt": 1774776000,
  "lastUsedAt": 0,
  "expired": false,
  "token": "sst_...."
}
```

Die Zeiten sind Unix-Sekunden, `lastUsedAt` 0 bedeutet "nie benutzt". `expired` ist `true`, sobald `expiresAt` erreicht ist; abgelaufene Tokens authentifizieren nicht mehr, bleiben aber in Liste und Detail sichtbar (siehe Lebenszyklus). **Der Klartext (`token`, 47 Zeichen: `sst_` und 43 Zeichen URL-sicheres Base64) steht nur in dieser einen Antwort** und ist danach nirgends mehr abrufbar. `GET` liefert dieselben Felder ohne `token`. Ausstellen und Widerruf werden mit Token-Id, Name, Ziel und handelndem Administrator (nie mit dem Token) als `[audit]`-Zeile auf stdout protokolliert.

### Rechte je Ziel

| Ziel | Lesen / Schreiben | `GET /projects` | `POST /projects` | `DELETE /projects/{id}` |
|---|---|---|---|---|
| `project` | nur die aufgefuehrten Projekte | nur diese | 403 | 403 |
| `group` | Projekte, deren `owningGroup` die Gruppe ist | nur diese | 201; `owningGroup` ist die Gruppe des Tokens, kein `owner` | 403 |
| `server` | alle Projekte, auch Bibliotheksprojekte | alle | 201; ohne `owner` und ohne `owningGroup` | erlaubt |

- Ein vom Client mitgeschickter `owner` oder `owningGroup` wird bei Token-Anfragen ignoriert.
- Nicht erlaubte Projekte antworten wie unbekannte mit 404 (keine Aufzaehlung). `DELETE` auf ein lesbares Projekt, das das Token nicht loeschen darf, antwortet 403.
- Von einem Server-Token angelegte Projekte haben keinen Eigentuemer und sind damit nur fuer Administratoren und Server-Tokens sichtbar, bis ein Administrator sie zuweist.
- Bibliotheksprojekte sind nur fuer Server-Tokens lesbar; ein Projekt-Token kann nicht auf eines ausgestellt werden.

### Zugriffsart

- `read`: nur `GET` und `HEAD`, zusaetzlich `POST /projects/{id}/query-results` (lesende Abfrage). Jede andere schreibende Methode (Commit, Branch, Tag, `PUT`, gespeicherte Query, `DELETE`, Projekt anlegen) antwortet 403 `{"error":"insufficient_scope"}`.
- `read_write`: alle Methoden, soweit das Ziel es erlaubt (Tabelle oben).

### Nur fuer Login-Sitzungen

Kein Token, auch kein Server-Token mit `read_write`, erreicht `/admin/*` (Benutzer- und Token-Verwaltung), `/groups/*`, `/me` und `/me/*` sowie `PUT /projects/{id}/owner`. Die Antwort ist 403 `{"error":"session_required"}`, unabhaengig von der Zugriffsart. Die Pruefung erfolgt im Filter auf dem Pfad, Gross-/Kleinschreibung und doppelte Schraegstriche spielen keine Rolle; nicht kanonische Pfade (`//`, `.`, `..`, `%`) werden fuer Tokens mit 403 `{"error":"insufficient_scope"}` abgelehnt. Sonst koennte ein geleaktes Server-Token Administratoren anlegen oder sich selbst verlaengern.

### Lebenszyklus

| Ereignis | Wirkung |
|---|---|
| Ablaufdatum erreicht | Token sofort ungueltig (401), `expired: true` in Liste und Detail. Es bleibt 30 Tage nach Ablauf erhalten (eine falsch gehende Systemuhr soll keine Tokens loeschen); danach loescht das Housekeeping (alle 5 Minuten) es aus Speicher und Datenbank. Ein Widerruf funktioniert wie bei jedem Token. Der Name ist schon vorher wieder frei: Beim Ausstellen mit diesem Namen wird das abgelaufene Token geloescht. Abgelaufene Tokens zaehlen nicht zu `api_token_max_count` |
| `DELETE /admin/tokens/{id}` | sofort ungueltig (401), auch wenn MongoDB gerade nicht erreichbar ist (dann 503, das Loeschen in der Datenbank wird wiederholt: Housekeeping, beim Beenden, erneutes `DELETE`). War das Loeschen beim Ende des Prozesses noch offen, ist das Token nach dem Neustart wieder gueltig und muss erneut widerrufen werden (der Server warnt dann beim Beenden mit den Ids) |
| Projekt geloescht | Das Projekt wird aus den Listen der Projekt-Tokens entfernt; ein Token ohne verbleibendes Projekt wird geloescht |
| Ausstellender Administrator geloescht oder herabgestuft | keine Wirkung, `createdBy` bleibt als Vermerk. Jeder Administrator kann das Token widerrufen |
| Passwort eines Benutzers geaendert (auch des Ausstellers) | keine Wirkung auf Tokens |
| Gruppe hat keine Mitglieder mehr | keine Wirkung, Gruppen sind nur Namen an Benutzern und Projekten |
| Neustart des Servers | Tokens bleiben gueltig (liegen in MongoDB), Login-Sitzungen nicht |

### Neue Konfiguration

Optionale Schluessel in `custom_config` (siehe [DEPLOYMENT.md](DEPLOYMENT.md)): `api_token_default_lifetime_days` (90), `api_token_max_lifetime_days` (365, hoechstens 365), `api_token_max_count` (200).

## Keine vorgeladenen Standardprojekte mehr

Eine neu angelegte Datenbank enthaelt keine Projekte aus der SysML-v2-Standardbibliothek mehr; das Verzeichnis `resources/sysml` entfaellt. Clients nutzen ihre lokale SysML-v2-Bibliothek. Bereits vorhandene Bibliotheksprojekte (`isLibrary: true`) bleiben unveraendert erhalten und sind weiterhin nur fuer Administratoren und Server-Tokens sichtbar. Wer sie nicht mehr braucht, loescht sie als Administrator ueber `DELETE /projects/{projectId}`.

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
