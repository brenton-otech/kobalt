# Sync server plan

This is a design proposal for a self-hosted Kobalt server on one small VPS. With one login per user, it would serve the user's books, settings and reading positions to every Kobalt device. Nothing here is implemented yet. Facts about Kobalt cite the file that shows them. Facts about other projects and the outside world give a source and the date they were checked, listed in [Sources](#sources).

## Summary

- Write a small custom server in Go: one static binary with SQLite in WAL mode and book files on the VPS disk. Put stock Caddy in front for HTTPS. Run it as a hardened systemd service under its own user, and back it up nightly with restic to off-site storage.
- Speak existing protocols where they give compatibility for free. OPDS 1.2 serves the catalog, so Kobalt's Kindle Catalog screen and KOReader work unchanged. KOSync-compatible endpoints serve positions, so Kobalt's current KOSync client and KOReader work unchanged. A small native JSON API covers what those protocols cannot express: settings, library listing with hashes, bulk download, tombstones and device registration.
- Devices never store the account password. Each device gets its own random credential, which the user can revoke without changing the password. The KOSync MD5 key and the `opds.json` password both become this revocable device credential.
- The owner manages accounts from a command line on the server. There is no open signup and no admin web interface on the public internet.
- Ship in five phases. Phases 1 and 2 need no Kobalt changes at all. Phases 3 to 5 add client code in `src/sync/` and `src/opds/`.

## What Kobalt has today

| Area | Current behavior | Where it is |
| --- | --- | --- |
| KOSync sign-in | Sends `x-auth-user` and `x-auth-key`, where the key is the MD5 hex digest of the password | `src/sync/kosync.cpp` (`Client::login` and `sendRequest`), [Sync data](sync-data.md#kosync-a-separate-sync-mechanism) |
| KOSync account storage | `kosync.json` keeps the server URL, username, the MD5 key, a device ID and sync preferences. Writes are atomic and owner-only where the file system allows. On Kobo's FAT storage it stays readable over USB. | [Sync and Android storage](storage/sync-and-android.md#kosync-account) |
| KOSync requests | Redirects are not followed, TLS verification stays on, each attempt has a ten-second deadline and the URL may have a path prefix | `src/sync/kosync.cpp`, [Progress sync](reader/sync.md#network-connections-and-errors), [KOSync validation](validation/kosync.md) |
| What a position is | A CREngine XPointer, a percentage from 0 to 1, a device name and a device ID. The document key is KOReader's partial-file MD5 or the MD5 of the file name. | `src/sync/kosync.h` (`documentDigest`, `filenameDigest`, `Progress`) |
| Conflicts | The client reads the server position before it uploads. A different remote position is never overwritten until the user chooses. | `Reconciler` in `src/sync/kosync.h`, [Choosing a synced position](reader/sync.md#choosing-a-synced-position) |
| Network on Kobo | Connections go through Nickel's Wi-Fi workflow | `platforms/kobo/common/native_sync_network.*`, `src/platform/api/sync_network.h` |
| OPDS catalog | Only the Kindle host connects it. It parses OPDS 1 Atom feeds, refuses DOCTYPE, sends HTTP Basic credentials only to the catalog's own scheme, host and port, and downloads one EPUB at a time. | `src/opds/feed.h` (`sameServer`), `src/opds/client.cpp`, [Kindle catalog](kindle-maintenance.md#catalog) |
| OPDS account storage | `opds.json` keeps `url`, `username`, `password` and `folder`, with the password in plain text | `src/opds/browser.h`, [Kindle catalog](kindle-maintenance.md#catalog) |
| OPDS download | The whole book is held in memory, up to 200 MB, before a worker thread writes it. A book is recognized by its `Author - Title.epub` file name, because a catalog gives no checksum. | `maxDownloadBytes` and `readAll` in `src/opds/client.cpp` |
| Kindle TLS | The package ships its own `libssl.so.3`, `libcrypto.so.3` (OpenSSL 3.5.9) and Qt's OpenSSL TLS plugin. No CA bundle is packaged, so trust depends on what the firmware provides. | `platforms/kindle/package.cmake`, [PW3 SDK](pw3-sdk.md#build-contract) |
| Kobo store books | Books that came through Nickel's sync, such as a Kobo-protocol server, use Nickel's position sync and are excluded from KOSync | [Sync data](sync-data.md#books-that-nickels-sync-carries) |
| Saved-data rules | Formats have their own versions, optional fields keep the version, and damaged data is never overwritten | [Storage compatibility](storage.md#rules) |

Two of these need attention regardless of the server:

- The KOSync key is password-equivalent. Anyone who copies `kosync.json` over USB can sign in as the user. MD5 is fast, so a copied key can also be brute-forced back to the password when that password is weak, and people reuse passwords.
- Holding a 200 MB download in memory is risky on a 512 MB Paperwhite 3. Bulk sync must stream to disk instead (see [Book download on the device](#book-download-on-the-device)).

## Goals and non-goals

Goals:

- One login per user covers books, settings and reading positions.
- The owner creates and removes users. There is no public registration.
- Kobo, Kindle PW3, PW5 and Colorsoft, Android and desktop Kobalt can all reach the server over HTTPS.
- A lost device can be cut off without changing the account password.
- One person can install, update, back up and restore the server in under an hour of routine work a month.
- Existing KOSync users and KOReader keep working.

Non-goals for the first phases:

- Public signup, payments, sharing books between users, or a store.
- Reading books in a browser. The server never renders book content.
- Format conversion such as EPUB to KEPUB, and metadata scraping from the internet.
- High availability. One VPS with tested backups is enough.
- Annotations, reading history and statistics sync. These come in phase 5.
- Kobo's store sync protocol. See [Open questions](#open-questions-for-the-owner).

## Threat model

The server is internet-facing and holds people's books, reading habits and credentials. These are the attackers in scope:

| Attacker | What they can do | Main defenses |
| --- | --- | --- |
| Internet scanners and bots | Probe ports, try default paths, guess passwords, send malformed requests | Only ports 80 and 443 open, SSH by key only, no admin interface on the public site, rate limits and fail2ban, argon2id hashes, no open signup, generic 404s |
| Network eavesdropper on hotel or café Wi-Fi | Read or change traffic | HTTPS only with HSTS, TLS 1.2 minimum, certificate verification always on in Kobalt, credentials never in URLs |
| Thief or finder of a device | Read the device's files, including `kosync.json` or `opds.json` over USB | Devices hold a revocable device credential, never the password. That credential cannot change the password, create credentials or delete data. The owner or user revokes it from the web page or the command line. |
| Another user on the same server, curious or malicious | Guess IDs, request other users' books or positions, upload hostile files | Every query is scoped by the authenticated user ID, per-user file storage, IDs that reveal nothing, upload validation, no server-side rendering |
| Someone who obtains a backup | Read the database and book files | restic encrypts backups on the VPS before upload. Password hashes are argon2id. Device credentials are stored only as SHA-256 hashes of high-entropy secrets. Backup credentials on the VPS cannot delete old snapshots. |
| A hostile EPUB uploaded by any user | Zip bombs, path traversal inside the archive, XML entity attacks, huge images | The server reads only the archive directory, `container.xml` and the OPF, with size limits, and never extracts to disk. See [Upload validation](#upload-validation). |
| A compromised dependency or update | Run code on the server | Few dependencies, pinned versions, Go's vulnerability checker in CI, an unprivileged sandboxed service |

Out of scope, stated so the owner can decide otherwise:

- The VPS provider and anyone with root on the VPS. They can read everything. Full-disk encryption on a rented VPS does not protect against the provider and makes unattended reboots hard.
- A device that is compromised while in the user's hands, for example by a malicious plugin. It can act as that device until revoked.
- Denial of service beyond simple rate limits. A small VPS can be overwhelmed; that costs availability, not data.

## Recommended architecture

```text
 Kobalt (Kobo, Kindle, Android, desktop)        Browser (upload, devices)
 KOReader                                        |
        |  HTTPS, TLS 1.2 or 1.3                 |
        v                                        v
 +---------------------------------------------------------+
 | Caddy: certificates, HTTP/2, HSTS, request size limits  |
 +---------------------------------------------------------+
        | HTTP on a Unix socket or 127.0.0.1
        v
 +---------------------------------------------------------+
 | kobalt-server (one Go binary, user "kobalt")            |
 |  /opds/...      OPDS 1.2 catalog and downloads          |
 |  /kosync/...    KOSync-compatible positions             |
 |  /api/v1/...    native JSON API                         |
 |  /account/...   small server-rendered web pages         |
 +---------------------------------------------------------+
        |                                 |
 /var/lib/kobalt-server/db/          /var/lib/kobalt-server/books/
 kobalt.sqlite (WAL)                 <user id>/<sha256 prefix>/<sha256>.epub
        |
 nightly: database snapshot, then restic to off-site storage
```

### Why this design

- A single static binary has one thing to update, and no runtime, interpreter or package tree to patch. Go's standard library covers HTTP, TLS client checks, crypto, XML, zip and path-safe file access. Go 1.24 added `os.Root`, which rejects `..` and symlink escapes from a directory, and Go 1.25 added cross-origin protection for form posts ([Go blog on os.Root](https://go.dev/blog/osroot)).
- SQLite holds a few users, devices, book rows and positions with no database server to run, tune or secure. The pure-Go `modernc.org/sqlite` driver keeps the binary free of cgo, so it builds as one static file.
- Caddy obtains and renews certificates by itself, which matters as Let's Encrypt shortens certificate lifetimes. Its defaults allow only TLS 1.2 and 1.3.
- A custom server can do the things no existing project does together: per-device credentials that work across OPDS, KOSync and a native API, settings sync, and a library change feed for bulk download.
- The server code lives in its own repository, not in the Kobalt repository. Kobalt's build is CMake and Qt, and `AGENTS.md` forbids adding other build tooling to it. The API contract is documented in the server repository. Kobalt's tests reach the server the same way `tests/support/run-kosync-server.sh` reaches the reference KOSync server today.

### Alternatives considered

| Option | What it gives | Why it was not chosen |
| --- | --- | --- |
| Compose existing pieces: Calibre-Web or Kavita for OPDS, plus korrosync or kosync-dotnet for positions, plus an identity provider | Mature catalog and book management with no code for phases 1 and 2 | Two or three separate account systems with separate passwords. No settings sync and no library change feed. More services to update. The OPDS password stays a full account password. |
| Extend Grimmory | One project with OPDS, KOReader sync, a Kobo connection, multiple users and OIDC | Java, Spring Boot and Angular with a separate MariaDB service, which is a poor fit for 1 to 2 GB of RAM. A large AGPL code base to fork and keep merging. Settings sync would still be new code. |
| Extend Komga or Kavita | OPDS, multiple users, and in Komga's case KOReader sync with an API key | JVM or .NET memory use on a small VPS. Komga's KOReader sync saves EPUB progress only at chapter starts, which loses Kobalt's exact positions. Kavita's own docs advise against its automatic KOReader sync. |
| Calibre content server with `--enable-auth` | Simple OPDS from an existing Calibre library | Needs Calibre on the server. One report describes KOReader failing against it with authentication on. No position or settings sync. |
| Rust instead of Go for the custom server | Smaller memory use and strong safety | Slower to write and change for one part-time maintainer. Go is already memory-safe enough for this, and its standard library covers more of the work without third-party crates. |
| Node.js or TypeScript | Fast to write, good libraries | A runtime and an npm dependency tree to keep patched. Less suited to a single-binary deployment. |
| Python with FastAPI or Django | Fast to write, Django has mature auth and admin | A runtime and virtual environment to maintain on the VPS, and higher idle memory. Not a problem for `AGENTS.md`, since the server is outside the app, but more to operate. |

### Existing projects checked

| Project | Stack and license | OPDS | KOSync endpoints | Multiple users | OIDC | Gaps for this plan |
| --- | --- | --- | --- | --- | --- | --- |
| koreader-sync-server (reference) | Lua on OpenResty, Redis, AGPL-3.0 | No | Yes, the reference | Yes, with open registration through `/users/create` | No | Stores the MD5 key. No books or settings. |
| kosync-dotnet | .NET, LiteDB, GPL-3.0 | No | Yes | Yes, with an admin API and `REGISTRATION_DISABLED` | No | The admin API authenticates with the MD5 of the admin password, and the default admin password is `admin` when unset. Maintenance status unclear. |
| korrosync | Rust, redb, MIT, version 0.4.6 of 2026-11-19 | No | Yes | Yes | No | Hashes stored keys with Argon2. No books or settings. Whether registration can be disabled was not found. |
| Calibre-Web | Python, GPL-3.0 | Yes | Not in the upstream README | Yes | LDAP, OAuth, proxy auth | Requires a Calibre `metadata.db`. Has Kobo store sync with Kepubify. |
| Calibre-Web Automated | Python, fork of Calibre-Web | Yes | Yes, since 3.1.0 | Yes | Inherited | How KOSync credentials are scoped per user was not documented where checked. |
| Kavita | C# on .NET, GPL-3.0 | Yes | Yes, since 0.8.7, with the API key in the URL | Yes | Yes | Its wiki advises against automatic sync. Users report 400 and 404 errors. A key in the URL ends up in logs. |
| Komga | Kotlin on the JVM | Yes | Yes, at `/koreader` with an API key | Yes | Yes | EPUB progress kept only at chapter starts. The JVM heap defaults to about 1 GB. |
| Grimmory, a community fork of BookLore | Java, Spring Boot, Angular, MariaDB, AGPL-3.0 | Yes | Yes, with separate KOReader credentials | Yes | Yes | Heavy for the target VPS, see above. Kobalt already accepts its KOSync replies ([Progress sync](reader/sync.md#network-connections-and-errors)). |
| Pocket ID | Go, single binary, SQLite by default | Not applicable | Not applicable | Yes | It is an OIDC provider using passkeys only | Useful only if the owner later runs several web apps. Not needed for one server; see [Optional second factor](#optional-second-factor-for-the-web-pages). |
| Authelia, Authentik, Keycloak | Identity providers | Not applicable | Not applicable | Yes | Yes | E-readers cannot do an OIDC browser login. They would still need device credentials, so a provider adds a service without removing work. |

Audiobookshelf was not evaluated in depth: it is built around audiobooks and podcasts, and none of the sources checked showed KOSync support.

## Backend choices

### Language and runtime

| Criterion | Go | Rust | Node.js or TypeScript | Python |
| --- | --- | --- | --- | --- |
| Deployment | One static binary | One static binary | Runtime plus packages | Runtime plus virtual environment |
| Expected idle memory | Tens of MB, to be measured | Lowest | Higher | Highest |
| Auth and crypto | `golang.org/x/crypto/argon2` (`IDKey` is argon2id), standard `crypto` | Mature crates | Mature packages | Mature packages, Django auth |
| SQLite | `modernc.org/sqlite`, no cgo | `rusqlite`, needs a C build | Native addon | Built in |
| File serving | `http.ServeContent` handles Range and `If-Range` | Framework dependent | Framework dependent | Framework dependent |
| Update burden | Rebuild with a new Go release | Rebuild, more crates | Frequent dependency updates | Frequent dependency updates |
| Speed of change for one person | Good | Slower | Good | Good |

The memory figures are expectations, not measurements. Phase 0 measures the real resident memory of the server under a small load.

Go is current and supported: Go 1.27.0 was released on 2026-08-19 and 1.27.4 on 2026-11-10. Go 1.25.0 had a cross-origin protection bypass (CVE-2025-47910) that 1.25.1 fixed, so always build with the latest patch release.

### Metadata storage

- Use SQLite in WAL mode with `synchronous=FULL` for account and credential changes, and `NORMAL` for position updates. Set a busy timeout and use one writer connection. This mirrors the profile database rules in [Database and recovery](storage/database.md#journal-and-durability).
- Postgres is the runner-up. It earns its keep only with many concurrent writers or several app servers, neither of which applies here, and it adds a service to secure and upgrade.

### Book files

- Keep books on the VPS disk, one folder per user: `books/<user id>/<first two hex digits>/<sha256>.epub`. Paths come only from the user ID and the content hash, never from a client.
- Do not deduplicate across users. Sharing one file between users would let one user learn whether another owns a book, and it complicates deletion. The disk cost for a few users is small.
- S3-compatible object storage is the runner-up, for when the library outgrows the VPS disk. Design the storage code behind a small interface so a later move is a contained change. Object storage would also need signed URLs or proxying, which adds a moving part.

### Backups

- Recommended: a nightly database snapshot made with SQLite's online backup or `VACUUM INTO`, then `restic backup` of the snapshot, the book folders and `/etc/kobalt-server/` to off-site storage. restic encrypts on the VPS before upload.
- The backup target should refuse deletion from the VPS. Use restic's REST server in append-only mode on another machine, or an S3 bucket with object lock. Append-only protects against a compromised VPS deleting backups, but not against an attacker with root on the backup host, so keep the backup host separate ([rest-server docs](https://pkg.go.dev/github.com/restic/rest-server)).
- Litestream is the runner-up for continuous database replication. Version 0.5.0 (October 2025) changed its storage format and moved to the pure-Go SQLite driver, and early adopters reported a bumpy migration that 0.5.1 fixed. A nightly snapshot is simpler and enough when losing up to a day of position changes is acceptable. Devices keep their own positions and re-upload them, so a day's loss heals itself.

What must be backed up:

| Item | Why |
| --- | --- |
| `kobalt.sqlite` snapshot | Users, password hashes, device credential hashes, book metadata, positions, settings, tombstones |
| `books/` | The books themselves |
| `/etc/kobalt-server/` | Configuration, including the server pepper |
| The Caddyfile and systemd units | Fast rebuild of the host |
| restic repository password | Kept off the VPS too, in the owner's password manager. Without it the backups cannot be read. |

### Reverse proxy and TLS

- Use Caddy from the Caddy project's own Debian package repository. It gets certificates from Let's Encrypt, renews them, redirects HTTP to HTTPS and serves HTTP/2. Its default minimum is TLS 1.2, it supports TLS 1.3, and its TLS 1.2 cipher list is limited to modern AEAD suites. Do not override the defaults.
- nginx is the runner-up. It needs certbot and hand-written TLS settings, which are more to get wrong. Traefik is built for container labels and adds complexity for one service.
- Rate limiting is not part of stock Caddy; it needs a separately built module. Do the rate limiting in the server instead and keep Caddy stock, so its package updates arrive through the system's security updates.
- Let's Encrypt moved its default chain to the "Generation Y" intermediates on 2026-05-13. They are cross-signed by ISRG Root X1 and X2, so a client that trusts ISRG Root X1 keeps working. The default certificate lifetime drops to 64 days in February 2027 and to 45 days in 2028. Caddy renews automatically, so no action is needed on the server.
- HSTS with a one-year `max-age`. Do not preload the domain until the setup has been stable for months.

### TLS on the devices

| Device | TLS library | Expected versions | Trust store | Status |
| --- | --- | --- | --- | --- |
| Kindle PW3, PW5, Colorsoft | Kobalt's own OpenSSL 3.5.9 with Qt's OpenSSL plugin | TLS 1.2 and 1.3 | Whatever certificate folders on the firmware Qt finds. Kobalt packages no CA bundle. | Not verified on a device. Whether firmware 5.13.5 trusts ISRG Root X1 is unknown. |
| Kobo, firmware 4.x (Qt 5) | Nickel's Qt 5 and its OpenSSL | Probably TLS 1.2 | Nickel's | Not verified. A forum report says some older firmware negotiated only TLS 1.0 or 1.1 with a test site. |
| Kobo, firmware 5 and 6 (Qt 6) | Nickel's Qt 6 and its OpenSSL | TLS 1.2 or 1.3 | Nickel's | Not verified |
| Android | Kobalt's OpenSSL build for Android | TLS 1.2 and 1.3 | Android system store | Not verified against this server |

- Do not enable TLS 1.0 or 1.1 on the server for any device. If a Kobo 4.x device cannot do TLS 1.2, that device stays on a local network or VPN instead.
- Phase 0 must test every device type against a staging host with a real Let's Encrypt certificate. On Kindle, the existing `device_info` report already records the TLS backend and library version (`platforms/kindle/device_info.cpp`).
- If the Kindle firmware lacks ISRG Root X1, the fix belongs in kobuild: ship a pinned CA bundle with the OpenSSL that kobuild already prepares, and point Qt at it. This follows the rule that TLS and SDK production belong to kobuild ([Kindle building](kindle-maintenance.md#building)). It is not an application-side workaround.
- A device with a wrong clock fails certificate checks. Kobalt's TLS error message already asks the user to check the device date ([KOSync validation](validation/kosync.md)).

### Hosting hygiene

- Run the server as a systemd service with a dedicated unprivileged user, not in a container. A container adds a daemon and image updates without adding isolation that systemd's sandboxing options do not already give. Podman with a systemd quadlet is the runner-up if the owner prefers images.
- Use the current Debian stable release with `unattended-upgrades` for security updates, including Caddy from its repository.
- Firewall with nftables (through `ufw` if preferred): allow 22, 80 and 443 only. Restrict 22 to known addresses or to a WireGuard interface where possible.
- SSH with keys only, no root login, no password login.
- fail2ban for SSH and for the server's own authentication-failure log lines.
- Secrets passed through systemd credentials (`LoadCredential=`), readable only by the service.
- Logs go to journald. They never contain passwords, tokens, `x-auth-key` values, cookies, book titles or file names. Log user IDs, device IDs, book IDs and status codes.

## Accounts and authentication

### Accounts

- The owner creates users on the server with `kobalt-server user add <name>`. The command prints a one-time setup link valid for 24 hours, where the user sets a password. The owner never sees or sends the password.
- `kobalt-server user disable`, `user delete`, `user reset-password` and `user list` complete the set. Deleting a user removes their rows and book folder at once; see [Privacy](#privacy-and-data-handling).
- There is no registration endpoint. The KOSync `POST /users/create` route answers 403 with a message that accounts are created by the server owner.
- Usernames are lowercase ASCII letters, digits, dots, hyphens and underscores, 3 to 32 characters. This keeps them easy to type on an e-ink keyboard and avoids Unicode lookalikes.

### Password storage

- Hash account passwords with argon2id using memory 64 MiB, 3 iterations and parallelism 1. This is RFC 9106's lower-memory recommendation with parallelism reduced for a 1 to 2 vCPU VPS. It is well above the OWASP minimum of 19 MiB with 2 iterations.
- Store the hash in PHC string form, so parameters can change later and a login rehashes with the new ones.
- Allow at most two hashes at once through a semaphore. This caps hashing memory at 128 MiB, so a burst of login attempts cannot exhaust a 1 GB VPS.
- Passwords are at least 12 characters. Check them against a small bundled list of common passwords. No composition rules.
- The account password is used only on the web sign-in page and when enrolling a device. It is never stored on a device.

### Device credentials

Each device gets its own secret. One secret serves all three protocols, so one revocation cuts off everything that device can do.

- The secret is 20 characters from a lowercase base32 alphabet without look-alike characters, shown in groups of four, such as `k7m2-x9qa-...`. That is about 100 bits of randomness, and it can be typed on an e-ink keyboard when needed. The server ignores hyphens and case.
- The server stores two SHA-256 hashes of it: one of the secret, one of the MD5 hex digest of the secret. No slow hash is needed, because the secret is random and long. The second hash lets KOSync clients authenticate (see below).
- The secret can be sent three ways:
  - As `Authorization: Bearer <secret>` on the native API. This is what new Kobalt code uses.
  - As the password in HTTP Basic on `/opds/`, with the account username. This is what Kobalt's current Catalog screen and KOReader send.
  - As `x-auth-key: md5(secret)` on `/kosync/`, with the username in `x-auth-user`. This is what Kobalt's current KOSync client and KOReader send.
- The account password is never accepted on `/opds/`, `/kosync/` or the native sync endpoints. Only device secrets are. A copied `kosync.json` or `opds.json` then exposes one revocable device credential, not the password that may be reused elsewhere.
- Each device row records a name, creation time, last use time and last IP address prefix. The user sees them on the Devices page and can revoke any device. The owner can do the same with `kobalt-server device revoke`.
- Device credentials do not expire on a timer, because an e-reader can stay offline for months. A credential unused for a set time, 365 days by default, is disabled and shown as expired. See [Open questions](#open-questions-for-the-owner).

Device scopes limit what a stolen device can do:

| Scope | Allows | Default for new devices |
| --- | --- | --- |
| `library:read` | OPDS, library listing, downloads | Yes |
| `positions:write` | Read and write positions | Yes |
| `settings:write` | Read and write settings | Yes |
| `library:write` | Upload books | No; only the web pages upload by default |
| Account changes | Password, devices, deleting books or the account | Never for devices; web session only |

### How a device gets its credential

There are two ways, and both end with the secret on the device and no password stored:

1. Web enrollment, which works with today's clients. The user signs in on the web, chooses Add device, names it, and gets the secret once. The user types the secret into Kobalt's KOSync password field or Catalog password field, or into KOReader.
2. Device enrollment, added in phase 3. Kobalt's new account screen sends the username and password once to `POST /api/v1/devices` and receives the secret. Kobalt keeps only the secret. A user with a second factor enabled gets a short pairing code from the web page instead and types that on the device.

### How this replaces today's weak points

- The KOSync MD5 key: the device stores `md5(device secret)` in `kosync.json` instead of `md5(password)`. The format of `kosync.json` does not change, so this needs no Kobalt release. The key is still a bearer credential, but it is random, scoped and revocable.
- The plain-text `opds.json` password: it becomes a device secret instead of the account password. It is still plain text on the device, which cannot be avoided without a hardware key store, but losing it costs one revocation. Phase 3 changes Kobalt to store the server account separately and stop asking for a password on the Catalog screen; see [Client changes](#client-changes-in-kobalt).

### Brute force and rate limits

- Count failures per username and per client IP address. After 5 failures for a username in 15 minutes, add a growing delay of 1, 2, 4 and up to 60 seconds before answering. Do not lock the account outright, so an attacker cannot lock a user out.
- Limit the authentication endpoints, including KOSync `GET /users/auth`, to 20 requests a minute per IP address, then answer 429 with `Retry-After`.
- Device secrets are long enough that guessing is hopeless, but the same per-IP limit applies to them, so log noise stays low.
- Every failure writes one log line in a fixed format with the client IP address. fail2ban bans an address after 20 failures in 10 minutes for one hour.
- Behind Caddy, take the client IP address only from Caddy's forwarded header, and only when the request came from Caddy's socket.

### Sessions on the web pages

- A web sign-in creates a random 256-bit session ID in a cookie named with the `__Host-` prefix, with `Secure`, `HttpOnly` and `SameSite=Strict`. The server stores only its SHA-256 hash.
- Sessions end after 2 hours idle and 7 days at most. Changing the password ends all sessions but leaves device credentials alone. A separate "Sign out everywhere" also revokes all devices.
- Form posts use Go's cross-origin protection plus the `SameSite` cookie. Responses set a strict Content Security Policy with no inline scripts.

### Optional second factor for the web pages

- Phase 1 offers TOTP for any user, and requires it for the owner's own account if the owner also uses the web pages.
- Passkeys (WebAuthn) are a later improvement. They need a WebAuthn library, which adds a dependency, and e-readers cannot use them anyway.
- An external identity provider such as Pocket ID is not recommended for one server. Devices would still need device credentials, so it adds a service without removing any work.
- Device enrollment by password is refused for users with a second factor; they use a pairing code from the web page.

### What old clients can support

- Kobalt's KOSync client sends custom headers with a fixed key, and the Kindle Catalog sends HTTP Basic. Both work with device secrets as described above, with no change.
- New Kobalt code uses a bearer token over HTTPS. Qt's `QNetworkRequest` sets any header on Qt 5.2 and Qt 6, so this works on every host.
- No device does OAuth, cookies or WebAuthn. The design does not need them.

## Authorization and isolation

### Per-user separation

- Every handler gets the user ID from the authenticated credential, never from the URL or the body. Every SQL query on user data has `WHERE user_id = ?`. Code review checks this, and a test matrix checks it (see [Testing](#testing-and-security-validation)).
- Book IDs in URLs are random 128-bit IDs, not database row numbers. A guessed ID from another user returns 404, the same answer as an ID that does not exist.
- A user's files live under their own folder. The server opens the books folder with `os.Root` once at start and builds every path from the user ID and the content hash, so no client input reaches a file path.
- The original file name is kept only as metadata. In `Content-Disposition` it is sent sanitized, with the RFC 6266 `filename*` form for non-ASCII names.

### Upload validation

Uploads come from the web page and, if granted, from devices with `library:write`.

1. Caddy limits the request body to 250 MB. The server enforces its own limit of 200 MB, matching Kobalt's download limit in `src/opds/client.cpp`.
2. The server streams the body to a temporary file in its own `tmp/` folder on the same disk, hashing it with SHA-256 and KOReader's partial MD5 as it writes. It never holds the whole file in memory.
3. It checks that the file is a ZIP archive whose first entry is `mimetype` with the content `application/epub+zip`.
4. It reads only the central directory. It refuses more than 10,000 entries, any entry name that is absolute or contains `..` or a backslash, and a total declared uncompressed size over 1 GiB. It does not extract anything to disk.
5. It reads `META-INF/container.xml` and the OPF through a size-limited reader, 1 MiB each, with a compression ratio check. It refuses any document with a DOCTYPE, as Kobalt's own feed parser does (`src/opds/feed.h`). Go's `encoding/xml` does not resolve external entities, so this is a second safeguard.
6. It takes title, authors, language and identifiers from the OPF, trimmed and length-limited. Cover images are left for a later phase; when they come, the server checks dimensions before decoding and caps pixel counts.
7. An upload whose SHA-256 matches a book the user already has returns that book (200) instead of a copy.
8. The file moves into place with a rename only after every check passes. A failed upload deletes its temporary file. A clean-up at start removes temporary files older than a day.

### The server never renders books

- No web page shows book HTML, CSS, images or scripts. The library page shows only the metadata strings from step 6, escaped by Go's `html/template`.
- Downloads are sent with `Content-Type: application/epub+zip`, `Content-Disposition: attachment`, `X-Content-Type-Options: nosniff` and `Content-Security-Policy: sandbox`. A browser saves the file and never runs it.
- Only EPUB is accepted at first. PDF and other formats are an open question; each format added needs its own validation step.

## API design

All three interfaces are served by the same binary under one host name. Kobalt's KOSync client already supports a path prefix ([Progress sync](reader/sync.md#kosync-accounts)), so `/kosync` works as a prefix.

### OPDS 1.2 catalog

Kobalt's Kindle Catalog and KOReader need nothing else. The server follows the OPDS 1.2 specification:

| Path | Feed |
| --- | --- |
| `GET /opds/` | Root navigation feed: Recently added, All books, Authors, Search |
| `GET /opds/books?page=N` | Acquisition feed of all books, newest first, 50 per page, with `rel="next"` |
| `GET /opds/new` | The 50 most recently added books |
| `GET /opds/authors` and `/opds/authors/{id}` | Navigation by author, then that author's books |
| `GET /opds/search?q=...` | OpenSearch results as an acquisition feed |
| `GET /opds/opensearch.xml` | The OpenSearch description |
| `GET /opds/books/{id}/file` | The EPUB download |

- Each entry has `id` as `urn:uuid:<book id>`, `title`, `author`, `updated`, and an acquisition link with `rel="http://opds-spec.org/acquisition"` and `type="application/epub+zip"`. Kobalt's parser only offers links that OPDS names as acquisition links with an EPUB type (`epubLink` in `src/opds/feed.h`).
- All links are relative or on the same origin, so Kobalt's `sameServer` rule sends credentials to them.
- Feeds carry an `ETag` and answer `If-None-Match` with 304.
- Unauthenticated requests get 401 with `WWW-Authenticate: Basic realm="Kobalt"`. OPDS 1.2 asks servers to support HTTP Basic over TLS.
- The OPDS root also has a link with `rel="http://kobalt.app/rel/server"` pointing at `/api/v1/info`. Old clients ignore it; new Kobalt code uses it to offer the native account. The rel value is a placeholder until the owner picks a URI.

### KOSync-compatible positions

These routes behave like the reference server so that Kobalt, KOReader and other clients work unchanged. The reference server's repository points to a third-party conformance suite (kosync-conformance); run it against this server in CI.

| Method and path | Behavior |
| --- | --- |
| `GET /kosync/healthcheck` | `{"state":"OK"}` |
| `POST /kosync/users/create` | 403 `{"message":"Accounts are created by the server owner."}` |
| `GET /kosync/users/auth` | 200 `{"authorized":"OK"}` for a valid username and `md5(device secret)`, else 401 |
| `GET /kosync/syncs/progress/{document}` | The stored position, or the reference server's empty answer when there is none |
| `PUT /kosync/syncs/progress` | Stores the position and answers with the document and the server's timestamp |

- `document` is the client's digest. The server accepts 32 lowercase hex characters only.
- Positions are stored by user and document digest, whether or not the server holds that book. That is how KOSync works today, and it keeps books from other sources syncing.
- The timestamp in answers is the server's own clock. The device's clock never decides order.
- Optional metadata that Kobalt sends with Send document metadata (`filename`, `title`, `authors`) is stored with the position when present. It helps the web page name positions for books that are not on the server.
- Exact response bodies, including the absent-progress answer that Kobalt accepts ([progress API contract](validation/kosync.md#progress-api-contract)), are copied from the reference server and pinned by tests.

### Native JSON API

The native API carries what OPDS and KOSync cannot. All requests need `Authorization: Bearer <device secret>` unless noted. Bodies are JSON in UTF-8, at most 1 MiB except uploads.

#### Server and device endpoints

`GET /api/v1/info` needs no credential and tells a client what the server offers:

```json
{ "api": 1, "server": "kobalt-server", "version": "0.3.0",
  "features": ["opds", "kosync", "positions", "settings", "library"],
  "limits": { "upload_bytes": 209715200 } }
```

`POST /api/v1/devices` enrolls a device with the account password, once:

```json
{ "username": "ana", "password": "...", "device_name": "Kindle Paperwhite (Kobalt)",
  "platform": "kindle-pw3", "client_version": "1.0-beta.10" }
```

The answer, 201, is the only time the secret is shown:

```json
{ "device_id": "dv_5k2...", "secret": "k7m2-x9qa-...", "scopes": ["library:read", "positions:write", "settings:write"] }
```

- `POST /api/v1/devices/pair` takes `{ "code": "ABCD-1234", "device_name": "..." }` instead of a password, for a code made on the web page. Codes are single use and expire after 10 minutes.
- `GET /api/v1/devices/self` returns this device's name, scopes and last use time.
- `DELETE /api/v1/devices/self` signs the device out by revoking its own credential.

#### Library endpoints

`GET /api/v1/library/changes?since=<cursor>&limit=500` returns the user's library as a change feed. Every insert, metadata change and deletion increments a per-user change counter. The first call uses `since=0`.

```json
{ "cursor": 1287, "more": false,
  "changes": [
    { "seq": 1281, "op": "upsert", "book": {
        "id": "bk_0f3a...", "sha256": "9b1c...", "kosync_md5": "5e8f...",
        "size": 1830233, "title": "Middlemarch", "authors": ["George Eliot"],
        "file_name": "Eliot - Middlemarch.epub", "added": "2026-11-20T10:02:11Z" } },
    { "seq": 1287, "op": "delete", "id": "bk_77c1...", "sha256": "41aa...", "deleted": "2026-11-24T08:00:00Z" } ] }
```

- `GET /api/v1/books/{id}/file` downloads the book. It sends `ETag: "<sha256>"`, `Content-Length` and `Accept-Ranges: bytes`, and answers `Range` with `If-Range` through `http.ServeContent`, so an interrupted download resumes.
- `POST /api/v1/books` uploads a book as the raw request body with `Content-Type: application/epub+zip` and an optional `X-File-Name` header. It needs `library:write`. Answers are 201 with the book, or 200 when the user already has it.
- Deleting books is only possible from the web pages. The deletion appears in the change feed as a tombstone.
- Tombstones are kept for 180 days. A client whose cursor is older than the oldest tombstone gets 410 and must list from `since=0` again.

#### Position endpoints

The native position API adds what KOSync lacks: versions, bulk reads and a conflict answer.

- `GET /api/v1/positions/{document}` returns the position with `ETag: "<version>"`:

  ```json
  { "document": "5e8f...", "version": 42, "progress": "/body/DocFragment[12]/body/div/p[7]/text().0",
    "percentage": 0.4731, "device": "Kobo Libra Colour (Kobalt)", "device_id": "1f0c...",
    "updated": "2026-11-24T21:14:03Z", "client_time": "2026-11-24T21:13:58Z" }
  ```

- `PUT /api/v1/positions/{document}` stores a position. The client sends `If-Match: "<version it last saw>"`, or `If-None-Match: *` for a first upload. A stale version gets 412 with the current position in the body, so the client can ask the user without another request.
- `GET /api/v1/positions/changes?since=<cursor>` lists positions changed since a cursor, for a device that wants all positions after a long time offline.
- KOSync and native writes share one table and one version counter, so a KOReader write is visible to Kobalt's native client and the other way round.

#### Settings endpoints

- `GET /api/v1/settings` returns the user's synced settings:

  ```json
  { "version": 17, "values": {
      "dark_mode": { "value": true, "version": 15, "device_id": "dv_5k2..." },
      "default_font_face": { "value": "Libron", "version": 17, "device_id": "dv_9qq..." } } }
  ```

- `PATCH /api/v1/settings` sends only changed keys, each with the version the client last saw for that key:

  ```json
  { "changes": [ { "key": "dark_mode", "value": false, "base": 15 },
                 { "key": "word_spacing", "remove": true, "base": 9 } ] }
  ```

  Changes whose `base` is current are applied in one transaction and the answer is the new document. Keys whose `base` is stale are skipped and listed in `conflicts` with the current value, while the other keys still apply.
- The server treats values as opaque JSON. It checks only that keys match `^[a-z0-9_]{1,64}$`, that one value is at most 64 KiB and that the whole document is at most 256 KiB. Kobalt decides which keys it sends and how it reads them.

### Errors

- Errors use `application/problem+json` (RFC 9457): `{ "type": "...", "title": "...", "status": 412, "detail": "..." }`. KOSync routes keep the reference server's error shapes instead.

| Status | Meaning |
| --- | --- |
| 400 | Malformed request or invalid field |
| 401 | Missing or invalid credential. The body never says whether the user exists. |
| 403 | The credential lacks the scope, or registration is disabled |
| 404 | Not found, including another user's IDs |
| 409 | The upload conflicts with a book being deleted |
| 410 | The change cursor is older than the kept tombstones |
| 412 | `If-Match` did not match; the body carries the current record |
| 413 | Over the size limit |
| 415 | Not `application/epub+zip` |
| 422 | The file failed EPUB validation; `detail` names the check |
| 429 | Rate limited; `Retry-After` says when to try again |
| 503 | Maintenance or a database lock timeout; safe to retry |

- Kobalt never follows redirects with credentials (`ManualRedirectPolicy` in `src/sync/kosync.cpp`), so the server must never answer API requests with a redirect. Caddy's HTTP to HTTPS redirect is the only one, and clients configured with `https://` never see it.

## Data model

### Tables

| Table | Key columns | Notes |
| --- | --- | --- |
| `users` | `id`, `username` (unique), `password_hash`, `totp_secret`, `disabled`, `created`, `setup_token_hash`, `setup_expires` | `totp_secret` is encrypted with a key from the service's credentials |
| `sessions` | `id_hash`, `user_id`, `created`, `last_seen`, `expires` | Web sessions only |
| `devices` | `id`, `user_id`, `name`, `platform`, `secret_hash`, `kosync_key_hash`, `scopes`, `created`, `last_used`, `last_ip_prefix`, `revoked` | Both hashes are SHA-256 and indexed |
| `pairing_codes` | `code_hash`, `user_id`, `expires`, `used` | Single use, 10 minutes |
| `books` | `id`, `user_id`, `sha256`, `kosync_md5`, `size`, `title`, `authors`, `language`, `identifiers`, `file_name`, `added`, `seq` | Unique on `user_id, sha256`. The file path is derived, not stored. |
| `positions` | `user_id`, `document`, `version`, `progress`, `percentage`, `device`, `device_id`, `updated`, `client_time`, `filename`, `title`, `authors`, `seq` | Primary key `user_id, document` |
| `position_history` | `user_id`, `document`, `version`, the same fields, `replaced` | The last 20 replaced positions per document, for recovery from a wrong jump |
| `settings` | `user_id`, `key`, `value`, `version`, `device_id`, `updated` | Primary key `user_id, key` |
| `tombstones` | `user_id`, `kind`, `ref`, `sha256`, `deleted`, `seq` | Kept 180 days |
| `counters` | `user_id`, `library_seq`, `position_seq`, `settings_version` | Monotonic per user |
| `auth_failures` | `key`, `count`, `window_start` | Can live in memory; persisted only to survive restarts |
| `schema_migrations` | `version`, `applied` | The server's own schema version |

- `documents` that a position refers to need not exist in `books`. The library page joins `positions.document` with `books.kosync_md5` to show progress beside a book.
- The server computes `kosync_md5` at upload with the same partial-file algorithm as Kobalt's `documentDigest` (`src/sync/kosync.h`) and KOReader. A shared test vector file in both repositories keeps them equal.
- Filename matching (`filenameDigest`) cannot be joined to books reliably, because the device's file name may differ. Positions still sync; they just do not show beside the book on the web page.

### Sync and conflict rules

Positions:

- The server stores the latest position and never merges two positions. Choosing between them is the reader's job, and Kobalt already does it: the `Reconciler` reads before it uploads and asks the user when the remote position differs ([Choosing a synced position](reader/sync.md#choosing-a-synced-position)).
- Order comes from the server's version counter, which increases with every accepted write. Device clocks are never used for ordering, because e-readers with a flat RTC battery or no network time often have wrong clocks. The device's own time is stored as `client_time` and shown only as information.
- KOSync writes have no version precondition, as in the reference protocol. They are accepted as the newest write. Kobalt's existing read-before-write rule makes this safe enough for Kobalt; KOReader behaves as it does with any KOSync server.
- Native writes carry `If-Match`. Two devices that read the same book offline both upload when they reconnect. The first wins. The second gets 412 with the first's position and asks the user, exactly as today's Reconciler does after a read.
- Every replaced position goes into `position_history`, so a later version of the web page or the reader can offer "go back to where I was on the Kobo yesterday".

Settings:

- Merge per key: edits to different keys on two devices both apply. For the same key, the first write to reach the server wins and the second device is told about the conflict.
- In a conflict, Kobalt keeps the server value and does not ask the user. Settings conflicts are rare and low-stakes, and a prompt on an e-reader for a font choice would annoy more than help. This is a decision for the owner; see [Open questions](#open-questions-for-the-owner).
- A new device's first sync pulls server settings before it pushes any, so a fresh install does not overwrite the user's settings with defaults. Only keys the user changed on the device after that are pushed.

Books:

- The library is a set of books keyed by content hash. Adding the same book twice is a no-op. Deleting is a tombstone. Devices apply the change feed in order and keep their cursor.
- A device never deletes a local file just because the server has a tombstone, unless the user turned that on. Files are the user's; a server mistake must not erase a device's library.

### Interaction with Kobalt's saved-data rules

The server does not change any Kobalt storage format by itself. The client changes follow [Storage compatibility](storage.md#rules):

- A new `server.json` profile file holds the server account: URL, username, device ID, device secret, cursors and preferences, with `schema: 1`. It follows the same rules as `kosync.json`: atomic writes, owner-only permissions where possible, invalid files moved to `backups/` by profile preparation, and an explicit default for every optional field.
- Synced settings are written into `reader-settings.json` as ordinary field changes through the profile writer. That file's rules already keep unknown keys and apply only changed fields ([Settings](storage/settings.md)). No schema change.
- Per-key sync state, such as the last seen version of each settings key, lives in a new `profile_state` record, for example `server-sync:settings`, with record format 1. Adding a record name needs no database schema change; check this against [Database and recovery](storage/database.md) when implementing.
- A server value for a key the device does not know is stored and left alone. A server value that is invalid for this device uses the device's default and is not pushed back.
- `kosync.json` is untouched. Existing KOSync accounts keep working whether or not the user adds a server account.

Device-local settings must never sync. The client keeps an explicit allowlist of portable keys. Proposed first split, to be confirmed with the owner:

| Sync | Keys |
| --- | --- |
| Yes | Theme and palette, `dark_mode`, interface fonts, header and footer layout and fonts, `default_font_face` and the generic family fallbacks, `page_margins`, `word_spacing`, `justification_tracking`, `widows_orphans`, `bold_reading`, hyphen and column settings, paragraph and normalization settings, `text_presets`, `new_book_text`, dictionary font settings, `save_looked_up_words`, `tap_zones`, `tappable_links`, `marks_sort`, `zen_mode` |
| No | `orientation`, `auto_rotate`, `locked_orientation`, `disable_reader_standby`, `screen_refresh`, `page_animations`, `page_turn_animation`, `button_actions`, `edge_light_gestures`, `sideloaded_fonts_first`, Library view and folder settings, anything with a path |

- `page_margins` are stored in 300 PPI pixels and apply the same physical margins at every density ([Settings](storage/settings.md#typography-layout-and-book-normalization)), so they are portable between a 300 PPI Kindle and a phone.
- A font name that is missing on another device already falls back to the default when loaded. That device does not push the fallback back to the server.
- Per-book text settings live in `book_state`, not in `reader-settings.json`. They are not synced in phase 3.

## Client changes in Kobalt

Phases 1 and 2 need no client changes. Phases 3 to 5 add:

| Change | Where | Notes |
| --- | --- | --- |
| Server account and HTTP client | `src/sync/server_client.*` (new) | Bearer requests with the same rules as `kosync::Client`: no redirects, TLS verification always on, bounded deadlines, cancellation, diagnostics without URLs or credentials |
| Server account storage | `src/sync/server_account.*` (new), `server.json` | Written on a worker thread, like the KOSync account |
| Position sync over the native API | `src/sync/kosync.*` and `sync_coordinator.*`, behind the same `Reconciler` | A second transport for the existing coordinator. Uses `If-Match` and the 412 answer. |
| Settings sync | `src/sync/settings_sync.*` (new) | Applies changes through the profile writer only |
| Bulk library sync | `src/sync/library_sync.*` (new), shown in `src/app/library/` | Downloads on a worker thread, see below |
| Catalog account | `src/opds/browser.*` | Uses the server account's device secret when the catalog is the user's Kobalt server |
| Settings screen | the existing Sync page | A Kobalt server section above KOSync, reusing the KOSync account editor |

### Keeping the main thread free

- The existing KOSync client sends requests through `QNetworkAccessManager` on the GUI thread, which is asynchronous and never blocks. Positions and settings can keep that pattern, because their bodies are small.
- Every storage write goes to a worker thread: the account file, settings fields and sync state, as the KOSync and OPDS code already do. Any new storage helper calls `mainthread::blocking()` (`src/support/main_thread.h`), and the native tests run with `KOBALT_STRICT_MAIN_THREAD=1`.
- Book downloads do not touch the GUI thread at all. The library sync runs in its own `QThread` with its own `QNetworkAccessManager`, and only sends progress events to the GUI.
- Applying downloaded settings while a book is open waits for the reader to be idle, as a position choice does today. Settings that need a relayout apply at the next book open, not under the user's finger.
- After the first shared edit, run `cmake -P platforms/kobo/syntax.cmake`, since the new code must build on Qt 5.2 and Qt 6.5.

### Book download on the device

The current Catalog download holds a whole book in memory. Bulk sync replaces that pattern:

1. Fetch the change feed in pages of 500 and store the cursor only after the page's changes are recorded locally.
2. Build a queue of missing books, smallest first, so many books arrive quickly and a large one cannot block the rest.
3. Download one book at a time. Stream it in chunks into `<name>.part` in the target folder, hashing as it writes. Never hold more than a small buffer, such as 256 KiB, in memory.
4. Resume an interrupted `.part` file with `Range` and `If-Range` set to the book's ETag. If the server answers 200 instead of 206, start again.
5. Check the SHA-256 against the change feed, then rename the file into place. A mismatch deletes the `.part` file and reports the book.
6. Before each book, check free space with `QStorageInfo`. Keep a reserve of 200 MB or 10 percent of the volume, whichever is larger. Stop with a clear message when space runs out.
7. Run only when the user starts it from the Library, or automatically only when Wi-Fi is already connected and no book is open. Pause when a book opens, because downloads compete for the CPU and storage that a page turn needs. Resume when the reader closes.
8. Never turn Wi-Fi on for a bulk sync. On Kindle, respect the Wi-Fi switch and Airplane mode as KOSync does ([KOSync and Wi-Fi](kindle-maintenance.md#kosync-and-wi-fi)). On Kobo, use `SyncNetwork::prepare` only with `interactive` true for a sync the user started, and release the connection afterwards.
9. Write the local record of which books came from the server in one batch per downloaded book, on the worker thread, not per chunk.

Each platform needs its own last step:

| Platform | Where books go | Open issue |
| --- | --- | --- |
| Kindle | The Catalog's chosen download folder, which becomes a Library folder | None beyond device testing |
| Android | A Library folder the user grants through the system's folder picker | Uses the same storage access path as today's Library folders |
| Desktop | A chosen Library folder | None |
| Kobo | The Kobo's user storage, where Nickel must import the file | Kobalt on Kobo uses Nickel's library. A downloaded file appears only after Nickel imports it, and how to trigger that safely is not established. See [Open questions](#open-questions-for-the-owner). |

Storage IO on Kobo and Kindle is expensive. Measure a bulk sync of 100 books on the slowest device and storage, and check page-turn latency while a sync runs in another state, before turning on any automatic sync.

### Existing KOSync users

- Nothing changes for them. `kosync.json`, its MD5 key and the KOSync settings stay as they are.
- A user who moves to the new server can keep using the KOSync screen with a device secret from the web page. They lose nothing and gain revocation.
- When the user signs in to a Kobalt server account, the Sync page offers to use it for positions. Accepting signs out of KOSync for that device; declining keeps both, with KOSync used for positions. One book never syncs through both.

### The plain-text password

- From phase 1, users put a device secret, not their password, into the Catalog account. The Catalog help text and [Kindle catalog](kindle-maintenance.md#catalog) should say so once the server exists.
- From phase 3, a Catalog address that points at a Kobalt server uses the server account instead, and `opds.json` keeps no password for it. Other OPDS catalogs keep working as today.
- Kobo and Kindle user storage is readable over USB and has no key store, so encrypting the secret on the device would only hide the key next to it. A scoped, revocable secret is the honest protection. Android can keep the secret in app-private storage.

## Deployment and operations

### Reference layout

| Path | Owner and mode | Contents |
| --- | --- | --- |
| `/opt/kobalt-server/releases/<version>/kobalt-server` | root, 0755 | Each release's binary |
| `/opt/kobalt-server/current` | root | Symlink to the running release, for one-step rollback |
| `/etc/kobalt-server/config.toml` | root:kobalt, 0640 | Host name, paths, limits, retention |
| `/etc/kobalt-server/credentials/` | root, 0600 | The pepper and TOTP encryption key, passed with `LoadCredential=` |
| `/var/lib/kobalt-server/db/` | kobalt, 0700 | `kobalt.sqlite` and its WAL |
| `/var/lib/kobalt-server/books/` | kobalt, 0700 | One folder per user |
| `/var/lib/kobalt-server/tmp/` | kobalt, 0700 | Upload staging on the same file system |
| `/var/backups/kobalt-server/` | kobalt, 0700 | The last two database snapshots |
| `/run/kobalt-server/http.sock` | kobalt, Caddy's group | The Unix socket Caddy proxies to |
| `/etc/caddy/Caddyfile` | root | The site definition |

A minimal Caddyfile:

```text
books.example.org {
    encode zstd gzip
    request_body {
        max_size 250MB
    }
    header Strict-Transport-Security "max-age=31536000"
    reverse_proxy unix//run/kobalt-server/http.sock
    log {
        output file /var/log/caddy/access.log {
            roll_keep_for 336h
        }
    }
}
```

The systemd unit uses these sandbox settings: `User=kobalt`, `NoNewPrivileges=yes`, `ProtectSystem=strict`, `ReadWritePaths=/var/lib/kobalt-server /var/backups/kobalt-server /run/kobalt-server`, `ProtectHome=yes`, `PrivateTmp=yes`, `PrivateDevices=yes`, `RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6`, `SystemCallFilter=@system-service`, `MemoryMax=512M` and `Restart=on-failure`. Check the unit with `systemd-analyze security kobalt-server`.

### Configuration

- One TOML file with every setting and its default listed in the server repository. Unknown keys stop the server at start, so a typo cannot silently fall back to a default.
- Secrets never go in the TOML file, environment variables or the command line. They come from `LoadCredential=`.
- `kobalt-server check-config` validates the file and the folders before a restart.

### Updates

1. Build the binary in CI with the latest Go patch release, run the tests, `govulncheck` and the KOSync conformance suite, and publish a checksum.
2. On the VPS, copy the release into `releases/<version>/` and verify the checksum.
3. Take a database snapshot with `kobalt-server backup`.
4. Point `current` at the new release and restart. Schema migrations run at start, in one transaction each, and refuse to run on a database newer than the binary.
5. If the health check at `/api/v1/info` fails, point `current` back and restore the snapshot if a migration ran.

- Database migrations follow the same rule as Kobalt's: never destroy data, and keep the previous release able to read the database for at least one release where possible.
- Debian and Caddy security updates install by themselves. Reboot monthly, or when `needrestart` asks.

### Monitoring and logs

- An external uptime check, from any free monitoring service, fetches `/api/v1/info` every 5 minutes and alerts by email.
- A daily systemd timer runs `kobalt-server report` and mails the owner: disk use, database size, failed logins, last successful backup and certificate expiry.
- Keep journald and Caddy logs for 14 days. Access logs contain paths with random IDs but no secrets, because no secret ever goes in a URL. This is one reason to avoid API keys in paths, as Kavita does.

### Backup and restore

- A systemd timer at night runs `kobalt-server backup` (online database snapshot) and then `restic backup` of the snapshot, `books/` and `/etc/kobalt-server/`.
- Retention: `restic forget --keep-daily 7 --keep-weekly 4 --keep-monthly 6`, run from a machine that has delete rights, not from the VPS.
- `restic check` weekly, and `restic check --read-data-subset 5%` monthly.
- The report mail includes the age of the last snapshot. A backup older than 36 hours is an alert.

Restore drill, every three months and after any change to the backup setup:

1. Create a fresh VPS or a local virtual machine.
2. Install the release, Caddy and restic from the written runbook only.
3. Restore the latest snapshot and `/etc/kobalt-server/`.
4. Start the server against a test host name.
5. Sign in as a test user, download a book, read and write a position with KOSync and the native API, and compare a book's SHA-256 with the original.
6. Record the date, the restore duration and any runbook fixes in the server repository.

### Disaster recovery

| Event | Recovery |
| --- | --- |
| VPS lost or provider account closed | Run the restore drill on a new host, then point DNS at it. Target: back within 4 hours, losing at most one day of changes. Devices re-upload newer positions themselves. |
| Database damaged | Stop the service and restore the last snapshot. Keep the damaged file for inspection; never delete it. |
| Server compromised | Take it offline. Rebuild from scratch, not from the old disk. Restore data from a backup taken before the compromise. Rotate the pepper, which invalidates all device credentials, and ask users to change their passwords. |
| Device credential leaked | Revoke the device. Nothing else changes. |
| Account password leaked | Reset it with the command line, end sessions, review the user's devices. |
| Domain lost | Devices store the URL, so each needs the new address once. Keep the domain on auto-renew with a registrar lock. |

### Hardening checklist

- [ ] SSH: keys only, no root login, no password authentication, limited to known addresses or WireGuard if possible.
- [ ] Firewall: only 22, 80 and 443 inbound. The server listens on a Unix socket only.
- [ ] `unattended-upgrades` enabled for security updates, with automatic reboots off and a monthly manual reboot.
- [ ] fail2ban jails for `sshd` and `kobalt-server` authentication failures.
- [ ] systemd sandboxing as listed, and `systemd-analyze security` reviewed.
- [ ] Caddy with default TLS settings, HSTS and the body size limit. No TLS 1.0 or 1.1.
- [ ] No registration endpoint, and no admin pages on the public site.
- [ ] Argon2id parameters and the hashing semaphore in place.
- [ ] Secrets only through `LoadCredential=`, and none in logs. Grep a day of logs for the test user's secret to prove it.
- [ ] restic backups encrypted, off-site, append-only from the VPS, and a restore drill recorded.
- [ ] An external scan with `testssl.sh` and `nmap` after every Caddy or firewall change.
- [ ] Owner account with TOTP if it uses the web pages.
- [ ] `govulncheck` clean on the deployed release.

## Privacy and data handling

What the server stores per user:

- Account: username, password hash, optional TOTP secret, creation time.
- Devices: name, platform, creation and last use times, the first three parts of the last IPv4 address or the first 48 bits of an IPv6 address, and credential hashes.
- Books: the files and their metadata.
- Positions: document digest, position, percentage, device name and ID, times, and the file name, title and authors if the device sends them.
- Settings: the synced keys and values.

Retention and deletion:

- Book files are the user's own. The owner does not read them, and the server never sends them anywhere but the user's devices.
- Replaced positions keep the last 20 per book. Tombstones are kept 180 days. Logs are kept 14 days.
- A user can download every book and a JSON export of positions and settings from the web page.
- Deleting an account removes the user's rows and book folder at once. The data stays in backups until they age out, which is at most six months with the retention above. The deletion page says so.
- No analytics, no third-party requests from the web pages, and no telemetry from the server.
- The owner can see who has an account and how much storage each uses. The command line does not print book titles unless asked for a specific user.

## Roadmap

Each phase ends with something usable.

| Phase | Scope | Done means | Tests |
| --- | --- | --- | --- |
| 0. Groundwork | Server repository, CI, staging host with a real certificate. A TLS probe of every device type. Memory measurement. Answers to the open questions. | Each device type has either reached the staging host over HTTPS or has a recorded fix, such as a kobuild CA bundle | A device test log per device type; `testssl.sh` report for the staging host |
| 1. Accounts, books and OPDS | User command line, setup links, web sign-in, upload page, devices page, device secrets, OPDS feeds and downloads, backups | A user uploads a book on the web, a Kindle PW3 downloads it through Kobalt's existing Catalog with a device secret, and a restore drill passes | Upload validation fuzzing, cross-user tests, Basic auth tests, OPDS parsing by Kobalt's `model.opds-feed` parser against server output, restore drill |
| 2. Positions | KOSync-compatible routes, native position API, position history | Kobalt on Kobo, Kindle and Android and KOReader exchange positions through `/kosync` with device secrets, with no Kobalt change | KOSync conformance suite, Kobalt's `model.kosync` and `lifecycle.kosyncFinal` server rows against this server, version and 412 tests |
| 3. Server account and settings in Kobalt | `server.json`, device enrollment and pairing, native positions in the client, settings sync with the key allowlist, Catalog using the server account | A setting changed on one device appears on another at its next sync, a device-local setting does not, and no password is stored on any device | New model tests for settings merge and conflict, storage compatibility tests for `server.json`, strict main-thread runs, Kobo syntax check, screen review of the new Sync page section |
| 4. Bulk library sync | Change feed client, streaming resumable downloads, free-space checks, Library entry point | A new Kindle PW3 gets a 100-book library unattended, survives Wi-Fi loss and a reboot mid-download, and page turns stay within the resource gate while a sync is paused | Download resume and hash tests against a local server, low-space tests, a device run on PW3 and on the slowest Kobo storage, release evaluation resource scenarios |
| 5. Annotations and history | Bookmarks, highlights, notes and reading sessions per book, with per-item merge | Highlights made on two devices both appear on both | Merge tests with the annotation limits in [Annotations and words](storage/annotations.md#limits), migration tests |

Phase 5 needs its own design. Annotations have revisions per list, limits and CREngine XPointer positions that differ between engines and layouts ([Annotations and words](storage/annotations.md)), and merging them is harder than last-writer-wins.

## Testing and security validation

On the server, in CI:

- Unit tests for every handler, including the 401, 403, 404, 412 and 429 paths.
- A cross-user matrix: two users, each with books, positions, settings and devices. Every endpoint is called with user A's credential and user B's IDs, and must return 404 or 403 with no data.
- Go fuzz tests (`go test -fuzz`) for the EPUB validator, the ZIP directory reader, the OPF parser, the KOSync request parser, the settings patch parser and the change cursor parser. Seed them with real EPUBs, crafted zip bombs, path traversal names and DOCTYPE documents.
- The KOSync conformance suite against the KOSync routes.
- A shared test vector file for the partial-file MD5, checked by both the server and Kobalt's `model.kosync` tests.
- `govulncheck` and `go vet` on every build.
- A log scan test: run the suite with known secrets and fail if any appears in the logs.

In Kobalt:

- Model tests for the new client against a local server binary, with an optional runner like `tests/support/run-kosync-server.sh`. The ordinary native suite stays offline.
- Storage compatibility tests for `server.json` and the new `profile_state` record, including damaged and newer files.
- Strict main-thread runs for every new path, and the source checks in `platforms/checks/`.
- Device checks on PW3, a Kobo with firmware 4.x and one with 5.x or 6.x, because native checks do not establish TLS or Wi-Fi behavior on a device.

Before opening the server to other users:

- An external scan from another network: `testssl.sh` for TLS, `nmap` for open ports, and an OWASP ZAP baseline scan of the web pages.
- A manual check that the admin command line is unreachable from the network and that no registration path exists.
- A recorded restore drill.

## Risks

| Risk | Effect | Mitigation |
| --- | --- | --- |
| Kindle firmware lacks a CA that Let's Encrypt chains to | Kindle cannot connect | Test in phase 0. Ship a CA bundle through kobuild if needed. |
| Kobo 4.x cannot negotiate TLS 1.2 | Old Kobos cannot connect | Test in phase 0. Those devices use a local network or VPN. |
| Importing downloaded books into Nickel is not safe or reliable | No bulk sync on Kobo | Keep Kobo on manual copies or a Kobo-protocol server until solved |
| Custom server code has a security bug | Data exposure | Small code base, cross-user tests, fuzzing, external scan, no rendering of user content |
| The owner stops maintaining the server | Unpatched internet-facing service | A single binary with few dependencies. The data is plain SQLite and files, and books can always be downloaded. |
| A one-person VPS fails at a bad time | Hours without sync | Devices keep working offline. Positions and settings are local first. |
| KOSync's MD5 key stays a bearer credential | A copied `kosync.json` gives access | It is a revocable device secret, not the password |
| A settings key changes meaning in a future Kobalt release | A synced value breaks an older device | Kobalt already uses defaults for invalid values. A key whose meaning changes gets a new name, per [Storage compatibility](storage.md#rules). |
| Bulk sync slows reading on slow devices | Page-turn latency | Pause while a book is open and measure with the resource gate |

## Open questions for the owner

1. How many users, and how large is the combined library in GB? This sets disk size, upload limits and whether object storage is needed soon.
2. Should the server be public, or reachable only over WireGuard or Tailscale? E-readers cannot run a VPN client, so a VPN-only server means syncing only at home or through a home router that joins the VPN. The recommendation is public with the hardening above, and the admin command line reachable only over SSH.
3. How long can a device be offline and still be trusted? This sets the unused-credential expiry, proposed at 365 days.
4. Should a settings conflict keep the server value silently, as proposed, or ask on the device?
5. Which settings should sync? Review the proposed allowlist.
6. Is there an existing Calibre library to import? If so, a one-time `kobalt-server import --calibre <folder>` that reads `metadata.db` read-only and copies EPUBs is a contained addition to phase 1.
7. Is a web page needed beyond upload, devices and account deletion? Every page is attack surface. A command-line upload tool is an alternative.
8. Which formats besides EPUB, if any? Each needs its own validation.
9. On Kobo, should books come through Kobalt's own downloads (which needs a safe Nickel import) or through a server that speaks Kobo's store sync, such as Calibre-Web, Grimmory or Komga? The second means those books use Nickel's position sync, not Kobalt's ([Sync data](sync-data.md#books-that-nickels-sync-carries)).
10. Which domain and which off-site backup target? A second machine with restic's REST server, or an S3 bucket with object lock?
11. Should the server code be open source, and under which license? This plan reuses no AGPL code, so the choice is open.

## Effort estimate

For one part-time developer, about 10 to 15 hours a week, with the server and the client being new code:

| Phase | Server | Kobalt client | Total |
| --- | --- | --- | --- |
| 0. Groundwork | 1 week | 1 week for device probes | 1 to 2 weeks |
| 1. Accounts, books and OPDS | 4 to 5 weeks | None | 4 to 5 weeks |
| 2. Positions | 2 weeks | None, plus test runs | 2 to 3 weeks |
| 3. Server account and settings | 1 week | 4 to 5 weeks, including screen review | 5 to 6 weeks |
| 4. Bulk library sync | 1 week | 5 to 7 weeks, longer if Kobo import is in scope | 6 to 8 weeks |
| 5. Annotations and history | 3 weeks | 6 or more weeks | 9 or more weeks |

These are rough. Device testing on Kindle and Kobo is usually where estimates slip.

## Recommended next step

Start phase 0 with two small, independent tasks:

1. Put a static test page behind Caddy with a real Let's Encrypt certificate on a staging host name. Point Kobalt's KOSync sign-in at it from a Kindle PW3, a Kobo on firmware 4.x, a Kobo on 5.x or 6.x and Android. Record which devices complete the TLS handshake. This answers the biggest unknown before any server code exists.
2. Answer open questions 1, 2, 9 and 10. They decide disk size, exposure, the Kobo approach and the backup target.

Then create the server repository with the skeleton, CI, `govulncheck`, the KOSync conformance suite and the shared MD5 test vectors, and begin phase 1.

## Sources

All checked on 2026-11-25.

| Fact | Source |
| --- | --- |
| Reference KOSync server: Lua on OpenResty, AGPL-3.0, MD5 key stored as sent, endpoints, conformance suite pointer | [koreader/koreader-sync-server](https://github.com/koreader/koreader-sync-server) |
| kosync-dotnet: .NET, LiteDB, GPL-3.0, `REGISTRATION_DISABLED`, admin API with MD5 headers, default admin password | [jberlyn/kosync-dotnet](https://github.com/jberlyn/kosync-dotnet) |
| korrosync: Rust, redb, MIT, version 0.4.6, Argon2 | [docs.rs korrosync](https://docs.rs/crate/korrosync/latest) |
| Grimmory: Java, Spring Boot, Angular, MariaDB, AGPL-3.0, OPDS, KOReader, Kobo, OIDC | [grimmory-tools/grimmory](https://github.com/grimmory-tools/grimmory) |
| Calibre-Web: Python, GPL-3.0, OPDS, Kobo sync, needs `metadata.db` | [janeczku/calibre-web](https://github.com/janeczku/calibre-web) |
| Calibre-Web Automated KOSync since 3.1.0 | [CWA 3.1.0 release notes](https://newreleases.io/project/github/crocodilestick/Calibre-Web-Automated/release/V3.1.0) |
| Kavita: C#, GPL-3.0, OPDS, OIDC | [Kareadita/Kavita](https://github.com/Kareadita/Kavita) |
| Kavita KOReader sync, API key in URL, advice against automatic sync, reported errors | [Kavita KOReader guide](https://wiki.kavitareader.com/guides/3rdparty/koreader/), [issue 4518](https://github.com/Kareadita/Kavita/issues/4518), [issue 4676](https://github.com/Kareadita/Kavita/issues/4676) |
| Komga KOReader sync at `/koreader`, API key, chapter-start EPUB progress | [Komga KOReader guide](https://komga.org/docs/guides/koreader/) |
| Komga JVM heap default near 1 GB, 512 MB too little | [Komga issue 23](https://github.com/gotson/komga/issues/23) and the Komga FAQ summarized in search results |
| Calibre content server authentication and a KOReader failure report | [calibre-server manual](https://manpages.debian.org/trixie-backports/calibre/calibre-server.1.en.html), [MobileRead thread](https://www.mobileread.com/forums/showthread.php?p=3899599) |
| Pocket ID: passkey-only OIDC provider, Go, SQLite by default | [pocket-id/pocket-id](https://github.com/pocket-id/pocket-id) |
| Go releases 1.27.0 and 1.27.4 | [Go release history](https://go.dev/doc/devel/release) |
| `os.Root` since Go 1.24 | [Go blog: Traversal-resistant file APIs](https://go.dev/blog/osroot) |
| CVE-2025-47910 in Go 1.25.0, fixed in 1.25.1 | [GO-2025-3955](https://pkg.go.dev/vuln/GO-2025-3955) |
| Argon2id in `golang.org/x/crypto/argon2`, RFC 9106 parameters | [argon2 package](https://pkg.go.dev/golang.org/x/crypto/argon2) |
| OWASP Argon2id minimums | [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html) |
| `modernc.org/sqlite` is a cgo-free SQLite driver | [modernc.org/sqlite](https://pkg.go.dev/modernc.org/sqlite) |
| Litestream 0.5.0 changes and early issues | [Fly.io announcement](https://fly.io/blog/litestream-v050-is-here/), [Hold off on Litestream 0.5.0](https://mtlynch.io/notes/hold-off-on-litestream-0.5.0/), [0.5.1 release](https://community.fly.io/t/litestream-v0-5-1-released/26176) |
| restic REST server append-only mode and its limits | [rest-server](https://pkg.go.dev/github.com/restic/rest-server) |
| Caddy TLS defaults | [Caddy tls directive](https://caddyserver.com/docs/caddyfile/directives/tls) |
| Rate limiting is a separate Caddy module | [mholt/caddy-ratelimit](https://www.github.com/mholt/caddy-ratelimit) |
| Let's Encrypt Generation Y, cross-signs, lifetime schedule | [Upcoming changes to Let's Encrypt certificates](https://community.letsencrypt.org/t/upcoming-changes-to-let-s-encrypt-certificates/243873), [Help Net Security on 64-day lifetimes](https://www.helpnetsecurity.com/2026/10/12/lets-encrypt-64-day-certificate-lifetime/) |
| OPDS 1.2 feeds, acquisition relations, pagination, search, Basic authentication | [OPDS 1.2 specification](https://specs.opds.io/opds-1.2) |
| Older Kobo firmware negotiating TLS 1.0 or 1.1, anecdotal | [MobileRead post](https://www.mobileread.com/forums/showthread.php?p=4317629) |

Not verified, and to be checked before relying on them:

- Which root certificates Kindle firmware 5.13.5 and the Colorsoft firmware provide, and which folders Qt's OpenSSL backend searches on those devices.
- The OpenSSL versions and TLS versions in Nickel on firmware 4.x, 5.x and 6.x.
- The real memory use of the proposed Go server, and argon2id timing on the chosen VPS.
- The exact response bodies of the reference KOSync server for every route; copy them from its source when implementing.
- Whether korrosync can disable registration, and how Calibre-Web Automated scopes KOSync credentials.
- Maintenance activity of kosync-dotnet, Calibre-Web and Kavita; their release pages showed no dates in the captures read.
- How Nickel can be asked to import a downloaded book safely.
