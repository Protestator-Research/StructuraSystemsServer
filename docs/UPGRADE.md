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
