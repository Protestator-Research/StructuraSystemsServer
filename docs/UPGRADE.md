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
