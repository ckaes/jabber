# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A minimal XMPP instant messaging and presence server (RFC 6120/6121) written in C11. Targets Pidgin compatibility and small deployments (≤10 concurrent users). No TLS, no database, no threading — uses poll-based non-blocking I/O and file-based user storage.

## Build & Run

```bash
make              # Build xmppd server and useradd tool
make xmppd        # Server binary only
make useradd      # User provisioning tool only
make clean

./xmppd -c config/xmppd.conf -D -L DEBUG   # Run with debug logging
./useradd -d ./data -u alice -p secret      # Add a user
```

Dependencies: `libxml2` (detected via `xml2-config`). Compiler: `cc` with `-std=c11 -Wall -Wextra -pedantic -g`.

## Architecture

**Core pattern:** Poll-based event loop → streaming SAX XML parser → stanza router → modular handlers → write buffer → TCP send.

**Session state machine:** `CONNECTED → STREAM_OPENED → AUTHENTICATED → BOUND → SESSION_ACTIVE`

**Key modules:**
- `server.c` — `poll()` event loop, non-blocking sockets, 8KB read buffers, write queue
- `xml.c` — libxml2 SAX2 push parser (streaming) + DOM tree per stanza; parser reset after auth
- `session.c` — Per-session state, XML parser context, presence cache, read/write buffers
- `stanza.c` — Stanza routing by element name/namespace to auth/roster/presence/message/disco handlers
- `presence.c` — Subscription management, roster-based broadcast, offline delivery on login
- `roster.c` — XML roster files in `data/<user>/roster.xml`, in-memory cache, roster push
- `message.c` — Online delivery or offline storage in `data/<user>/offline/NNNN.xml` with XEP-0203 delay
- `auth.c` — SASL PLAIN (base64 decode `\0user\0pass`), validated against `data/<user>/user.conf`

**File-based storage layout:**
```
data/
└── <username>/
    ├── user.conf      # password = <plaintext>
    ├── roster.xml     # contact list
    └── offline/       # offline message queue
```

## Configuration

Copy `config/xmppd.conf.example`. Key fields: `domain`, `port` (default 5222), `bind_address`, `datadir`, `logfile`, `loglevel` (DEBUG/INFO/WARN/ERROR).

CLI flags override config file: `-c`, `-d` (domain), `-p` (port), `-D` (datadir), `-l` (logfile), `-L` (loglevel).

## XMPP Feature Scope

Implemented: SASL PLAIN auth, resource binding, session establishment, roster get/set/remove, presence with subscriptions (subscribe/subscribed/unsubscribe/unsubscribed), message routing (online and offline), XEP-0030 service discovery basics, stanza error responses.

Not implemented: TLS/STARTTLS, multi-resource, S2S federation, MUC, vCards, MAM, SCRAM auth.

## Specification

`XMPP Prototype Spec for Agent` (982 lines in repo root) is the authoritative design doc covering data structures, RFC compliance details, all stanza types, error handling, and test scenarios. Consult it before implementing new features.
