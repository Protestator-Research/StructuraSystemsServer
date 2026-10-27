# Structura Systems Server

This is the Server implementation of SysMLv2. Currently this all works as a testing platform considering our digital tiwn implementation. But it might be schedules as a full release.

## Motivation

This system is a test platform for the SysMLv2 Library we are currently developing. Also the digital twin development shoould be supported to have a separation between the individual software.

This Software part will be further extended with additional features. Stay tuned!

## Sicherheit / Betrieb

Der Server laeuft hinter einem Reverse Proxy mit TLS und lauscht nur lokal. Konfiguration, Umgebungsvariablen, Reverse-Proxy-Beispiele und die Rotation eingecheckter Zugangsdaten stehen in [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md). Aenderungen beim Update und die Datenbankmigration beschreibt [docs/UPGRADE.md](docs/UPGRADE.md).

## Licensing

This software is licensed under the GPL v3, allowing a free and open source editing of the code. We want to offer you the option to use this libary also for commercial project. This will be done in a later step, since we are currently developing this from the ground up.
