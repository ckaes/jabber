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
## Workflow Orchestration

### 1. Plan Mode Default
- Enter plan mode for ANY non-trivial task (3+ steps or architectural decisions)
- If something goes sideways, STOP and re-plan immediately - don't keep pushing
- Use plan mode for verification steps, not just building
- Write detailed specs upfront to reduce ambiguity

### 2. Subagent Strategy to keep main context window clean
- Offload research, exploration, and parallel analysis to subagents
- For complex problems, throw more compute at it via subagents
- One task per subagent for focused execution

### 3. Self-Improvement Loop
- After ANY correction from the user: update 'tasks/lessons.md' with the pattern
- Write rules for yourself that prevent the same mistake
- Ruthlessly iterate on these lessons until mistake rate drops
- Review lessons at session start for relevant project

### 4. Verification Before Done
- Never mark a task complete without proving it works
- Diff behavior between main and your changes when relevant
- Ask yourself: "Would a staff engineer approve this?"
- Run tests, check logs, demonstrate correctness

### 5. Demand Elegance (Balanced)
- For non-trivial changes: pause and ask "is there a more elegant way?"
- If a fix feels hacky: "Knowing everything I know now, implement the elegant solution"
- Skip this for simple, obvious fixes - don't over-engineer
- Challenge your own work before presenting it

### 6. Autonomous Bug Fixing
- When given a bug report: just fix it. Don't ask for hand-holding
- Point at logs, errors, failing tests -> then resolve them
- Zero context switching required from the user
- Go fix failing CI tests without being told how

## Task Management
1. **Plan First**: Write plan to 'tasks/todo.md' with checkable items
2. **Verify Plan**: Check in before starting implementation
3. **Track Progress**: Mark items complete as you go
4. **Explain Changes**: High-level summary at each step
5. **Document Results**: Add review to 'tasks/todo.md'
6. **Capture Lessons**: Update 'tasks/lessons.md' after corrections

## Core Principles
- **Simplicity First**: Make every change as simple as possible. Impact minimal code.
- **No Laziness**: Find root causes. No temporary fixes. Senior developer standards.
- **Minimal Impact**: Changes should only touch what's necessary. Avoid introducing bugs.
