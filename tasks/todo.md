# Plan: Go Rewrite of XMPP Server (Reviewed)

## Context

The existing XMPP server is C11 + libxml2, poll()-based event loop, manual memory. Rewriting in
Go gives us goroutines (eliminating poll complexity), GC, `encoding/xml`, and faster iteration.
The existing Python integration tests (62 scenarios) are the acceptance criterion.

This plan was reviewed against the C source. Changes from initial plan are marked **[REVISED]**.

---

## Package Structure

```
jabber/
├── go.mod                       # module jabber, go 1.22, no external deps
├── main.go                      # xmppd: signal handling, context, server.Run()
├── cmd/useradd/main.go          # useradd: -d -u -p flags, identical on-disk layout
├── internal/
│   ├── xml/
│   │   ├── node.go              # XMLNode, XMLAttr, Serialize(), FindChild(), Attr()
│   │   └── decoder.go           # StreamDecoder: Token loop over encoding/xml
│   ├── user/user.go             # Create, Exists, CheckPassword
│   ├── roster/roster.go         # Load, Save, Find, AddOrUpdate, Remove, Push (offline variant)
│   ├── session/session.go       # Session struct, state machine, Send(), Close()
│   ├── server/server.go         # TCP listener, session registry, sessionLoop()
│   ├── stream/stream.go         # HandleOpen, SendError
│   ├── stanza/stanza.go         # Route() → RouteResult, SendStanzaError(), Send()
│   ├── auth/auth.go             # SASL PLAIN handler
│   ├── register/register.go     # XEP-0077 in-band registration
│   ├── presence/presence.go     # Broadcast, subscriptions, BroadcastUnavailable
│   ├── message/message.go       # Handle, StoreOffline, DeliverOffline
│   └── disco/disco.go           # HandleInfo, HandleItems (XEP-0030)
├── Makefile
├── config/                      # UNCHANGED
├── data/                        # UNCHANGED
└── tests/                       # UNCHANGED
```

No external dependencies — stdlib only.

**[REVISED]** No `internal/config` or `internal/log` packages — use stdlib `log/slog` directly
and a simple config struct in `server/`. Eliminates two packages that would have been wrappers
around stdlib anyway.

---

## Core Data Structures

### RouteResult **[REVISED — replaces CloseAfterWrite/StreamRestartPending flags]**

```go
// internal/stanza/stanza.go
type RouteResult int

const (
    RouteOK            RouteResult = iota
    RouteRestartStream             // auth succeeded; create new XML decoder
    RouteClose                     // stream error sent; close connection
)
```

In C, `CloseAfterWrite` and `StreamRestartPending` were boolean fields on the session struct
because SAX callbacks fired mid-`xmlParseChunk()` — you couldn't act immediately. In Go,
`ReadStanza()` returns to `sessionLoop` before anything else runs, so the control signal belongs
as a return value from `Route()`, not as persistent state. This makes the flow explicit and
testable without mock sessions.

### Session (`internal/session/session.go`)

```go
type State int

const (
    StateConnected State = iota
    StateStreamOpened
    StateAuthenticated
    StateBound
    StateSessionActive
    StateDisconnected
)

type Session struct {
    // Identity (set at auth / bind time, then immutable)
    JIDLocal, JIDDomain, JIDResource string
    State       State
    Available   bool
    InitialPresenceSent bool

    PresenceStanza *xml.XMLNode  // last available presence stanza

    // Roster (accessed only from this session's goroutine except offline-user mutations)
    Roster   roster.Roster
    RosterMu sync.Mutex

    // I/O
    conn net.Conn
    br   *bufio.Reader    // persists across stream restarts; xml.Decoder wraps this
    Dec  *xml.StreamDecoder

    // Lifecycle
    closed sync.Once      // [REVISED] ensures teardown runs exactly once
    writeMu sync.Mutex    // guards conn.Write calls from other goroutines
    dead    atomic.Bool   // [REVISED] set before conn.Close; Send() checks this
}

// BareJID returns "local@domain" or "" if not yet authenticated.
func (s *Session) BareJID() string { ... }

// FullJID returns "local@domain/resource" or "" if not yet bound.
func (s *Session) FullJID() string { ... }

// Send writes data to the session. Safe to call from any goroutine.
// Returns immediately if the session is already dead.
func (s *Session) Send(data string) {
    if s.dead.Load() {
        return
    }
    s.writeMu.Lock()
    defer s.writeMu.Unlock()
    s.conn.Write([]byte(data))   // error ignored; dead.Load check is the guard
}
```

**[REVISED]** `StreamRestartPending` and `CloseAfterWrite` removed. `dead atomic.Bool` replaces
the need for a closed-channel approach while staying simple. `sync.Once` on teardown prevents
double-cleanup from concurrent kick + natural exit.

### Server (`internal/server/server.go`)

```go
type Server struct {
    Domain  string
    DataDir string
    // ... other config fields inline; no separate config package

    mu        sync.RWMutex
    byBareJID map[string]*session.Session  // post-bind sessions only
    all       []*session.Session           // every connected session (snapshot under RLock)

    fileMu sync.Mutex  // [REVISED] serializes offline-user roster/file writes
}

// Register adds a newly-bound session. Replaces any existing entry.
// Returns the old session if one existed (caller must kick it).
func (srv *Server) Register(s *session.Session) (old *session.Session)

// Unregister removes a session. Only deletes byBareJID[jid] if it equals s —
// handles the kick-then-unregister race. [REVISED]
func (srv *Server) Unregister(s *session.Session)

// FindByBareJID returns the live session for a bare JID, or nil.
func (srv *Server) FindByBareJID(bareJID string) *session.Session

// AllSessions returns a snapshot copy of all sessions (safe to iterate outside mu).
func (srv *Server) AllSessions() []*session.Session
```

### XMLNode (`internal/xml/node.go`)

```go
type XMLAttr struct{ Name, NS, Value string }

type XMLNode struct {
    Name, NS string
    Attrs    []XMLAttr
    Children []*XMLNode
    Text     string
    // No Parent field — not needed; we pass context explicitly where required [REVISED]
}

func (n *XMLNode) Attr(name string) string
func (n *XMLNode) FindChild(name string) *XMLNode
func (n *XMLNode) FindChildNS(name, ns string) *XMLNode
func (n *XMLNode) Serialize() string          // delegates to serialize("", &b)
func (n *XMLNode) serialize(parentNS string, b *strings.Builder)  // [REVISED: strings.Builder]
```

**[REVISED]** Removed `Parent *XMLNode`. In C, libxml2's tree navigation goes both directions;
we never needed upward traversal here. Removing it simplifies memory reasoning.
`strings.Builder` instead of `bytes.Buffer` — idiomatic Go for string building.

---

## Concurrency Model

**One goroutine per TCP session** — blocks on `StreamDecoder.ReadStanza()`. The C version's
`parser_reset_pending` / `teardown_pending` / `in_xml_parse` flags exist solely because SAX
callbacks fire mid-`xmlParseChunk()`. None of those flags exist in Go.

```go
// internal/server/server.go
func (srv *Server) sessionLoop(s *session.Session) {
    defer func() {
        s.closed.Do(func() {           // [REVISED] sync.Once prevents double-teardown
            srv.Unregister(s)          // remove from registry before broadcasting
            presence.BroadcastUnavailable(srv, s)
            s.dead.Store(true)         // stop accepting new writes
            s.conn.Close()             // unblocks any pending writeMu waiter
        })
    }()

    for {
        node, err := s.Dec.ReadStanza()
        if err != nil {
            break   // EOF or XML parse error → defer runs
        }

        result := stanza.Route(srv, s, node)

        switch result {
        case stanza.RouteRestartStream:          // [REVISED] return value, not flag
            s.Dec = xml.NewStreamDecoder(s.br)  // new decoder, same bufio.Reader
        case stanza.RouteClose:
            return                               // defer handles cleanup
        }
    }
}
```

**Why defer order matters:**
1. `Unregister(s)` first — removes s from `byBareJID` and `all`. After this, no new goroutine
   can look up s via `FindByBareJID`.
2. `BroadcastUnavailable` second — iterates `AllSessions()` snapshot, sends to OTHER sessions.
   s is already gone from the registry so we won't send unavailable to ourselves.
3. `dead.Store(true)` — marks session as dead so late Send() calls from other goroutines return
   immediately rather than racing with conn.Close().
4. `conn.Close()` last — ensures all writes from step 2 have been sent before closing.

**Locking rules (total order prevents deadlocks):**
- `srv.mu` (RWMutex): held only for registry mutations and lookups. Never held during I/O.
- `session.writeMu` (Mutex): held during `conn.Write()`. Never nested with `srv.mu`.
- `session.RosterMu` (Mutex): held when mutating a session's roster from ANOTHER goroutine
  (e.g., subscription handling modifying an offline user's disk file). Acquired only after
  `srv.mu` is released.
- `srv.fileMu` (Mutex): **[REVISED]** held when reading/writing offline-user roster XML files
  on disk (the path where target session is offline and we're modifying their roster.xml).

**Bind conflict — race guard: [REVISED]**

```go
// In session_handle_bind (runs inside new session's goroutine):
srv.mu.Lock()
old := srv.byBareJID[bareJID]   // may be nil
srv.byBareJID[bareJID] = s      // register new session first
srv.mu.Unlock()

if old != nil {
    old.Send(streamErrorConflict)  // Send() is goroutine-safe via writeMu
    old.conn.Close()               // causes old goroutine's ReadStanza() → EOF
    // OLD GOROUTINE'S DEFER RUNS NATURALLY — do NOT call teardown here.
    // When old goroutine's Unregister() runs, it will see byBareJID[jid] == s (new),
    // not old, and skip the delete. [REVISED — critical correctness fix]
}
```

The old goroutine exits cleanly through its own defer. Trying to call teardown from two places
was the source of use-after-free bugs in an earlier C version.

---

## XML Streaming (`internal/xml/decoder.go`)

XMPP is an ongoing stream. `StreamDecoder` wraps `encoding/xml.Decoder` with depth tracking.
`encoding/xml` resolves namespace prefixes and sets `xml.Name.Space = URI`; xmlns pseudo-
attributes are not returned in `Attr` slices — this is simpler than the C libxml2 approach.

```
Token loop:
  ProcInst (<?xml ...?>)  → skip (sent by clients before <stream:stream>)
  StartElement at depth 0 → return special sentinel node (stream:stream open)
  StartElement at depth 1 → allocate root of new stanza tree, depth=1
  StartElement at depth>1 → push child, depth++
  CharData at depth ≥ 1   → append to current node's Text
  EndElement: depth-- → if depth becomes 0: return completed stanza node
                         else: pop, attach to parent, continue
  EndElement at depth 0   → stream close; return io.EOF sentinel
```

**Stream open sentinel:** Return a sentinel `*XMLNode{Name:"stream", NS:"http://etherx.jabber.org/streams"}` so `sessionLoop` can dispatch to `stream.HandleOpen`.

**[REVISED] Detecting stream:stream vs. stanza:**
```go
func (n *XMLNode) IsStreamOpen() bool {
    return n.Name == "stream" && n.NS == "http://etherx.jabber.org/streams"
}
```

**[REVISED] Distinguishing EOF from parse error:**
```go
node, err := s.Dec.ReadStanza()
if err == io.EOF {
    break  // clean disconnect — no error log
}
if err != nil {
    slog.Warn("XML parse error", "jid", s.BareJID(), "err", err)
    s.Send(streamErrorInvalidXML)
    break
}
```
The C version treated all errors the same. Go lets us be precise.

**Stream restart after auth:** `auth.HandleSASL` returns `RouteRestartStream`. The `sessionLoop`
then does `s.Dec = xml.NewStreamDecoder(s.br)`. Because `s.br` is the persistent `*bufio.Reader`
wrapping the conn, the new decoder reads from exactly the byte position where the old one stopped,
including any bytes already buffered. This is the same mechanism as the C version but expressed
as a return value rather than a deferred flag.

---

## Shutdown (`main.go`) **[REVISED]**

```go
func main() {
    // ... parse flags and config ...
    ctx, cancel := context.WithCancel(context.Background())
    defer cancel()

    sigCh := make(chan os.Signal, 1)
    signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
    go func() { <-sigCh; cancel() }()

    srv.Run(ctx)  // blocks until ctx is done
}

// internal/server/server.go
func (srv *Server) Run(ctx context.Context) {
    ln, _ := net.Listen("tcp", addr)
    go func() {
        <-ctx.Done()
        ln.Close()   // unblocks Accept() below
    }()

    for {
        conn, err := ln.Accept()
        if err != nil {
            if ctx.Err() != nil { return }  // intentional shutdown
            slog.Error("accept error", "err", err)
            continue
        }
        s := session.New(conn, srv.Domain)
        srv.addToAll(s)
        go srv.sessionLoop(s)
    }
}
```

**[REVISED]** No global `shutdown_flag`. `context.Context` + `ln.Close()` is the idiomatic Go
shutdown pattern. Each session goroutine exits naturally when its connection closes.

---

## Logging **[REVISED]**

Use `log/slog` (stdlib since Go 1.21) instead of a custom log module:

```go
// main.go
w := os.Stderr                          // or open logfile
if logFile != "" { w, _ = os.OpenFile(logFile, ...) }
level := parseLevel(cfg.LogLevel)       // INFO, DEBUG, etc.
slog.SetDefault(slog.New(slog.NewTextHandler(w, &slog.HandlerOptions{Level: level})))
```

Log XML in/out at DEBUG level via `slog.Debug("xml >", "data", xmlStr)`.

This eliminates `internal/log` entirely — no wrapper package needed.

---

## File Layout Compatibility (unchanged from original plan)

```
data/<username>/
  ├── user.conf               # password = <plaintext>\n
  ├── roster.xml              # <?xml...?>\n<roster>...</roster>
  └── offline/
      ├── 0001.xml            # XEP-0203 delay stamp='YYYY-MM-DDTHH:MM:SSZ'
      └── NNNN.xml            # zero-padded, sequential
```

**Offline message delivery (same as original plan):**
- Stamp: `time.Now().UTC().Format("2006-01-02T15:04:05Z")`
- Next seq: max NNNN from `os.ReadDir()` + 1
- Deliver in lexicographic order; delete each file after delivery

**[REVISED] Offline roster write protection:**
Subscription handlers that modify an offline user's roster.xml hold `srv.fileMu` for the
duration of the read-modify-write. With ≤10 users this is a single global mutex and is
never contended in practice, but it prevents a theoretical race if two sessions simultaneously
send subscriptions to the same offline user.

---

## Subscription State Machine (unchanged from original plan)

```
subscribe sent by A to B:
  A's roster: ensure entry for B exists with ask=true; save; push to A
  If B online: deliver subscribe stanza to B

subscribed sent by B to A:
  B's roster: none→from, to→both; save; push to B
  A's roster (online or disk): none→to, from→both; clear ask; save; push to A if online
  If A online: send B's current presence + subscribed notification to A

unsubscribe sent by A to B:
  A's roster: to→none, both→from; clear ask; save; push to A
  B's roster (online or disk): from→none, both→to; save; push to B if online
  If B online: deliver unsubscribe + unavailable-from-A if A available

unsubscribed sent by B to A:
  B's roster: from→none, both→to; save; push to B
  A's roster (online or disk): to→none, both→from; clear ask; save; push to A if online
  If A online: deliver unsubscribed + unavailable-from-B if B available
```

**On initial presence (single goroutine, sequential — no race possible):**
1. Load roster if not loaded
2. Broadcast our presence to contacts with from/both subscription
3. Send contacts' current presence to us (contacts with to/both subscription)
4. Deliver offline messages (sorted by filename)
5. Re-deliver pending subscribe requests from contacts with ask=true in THEIR roster for us

---

## Makefile **[REVISED]**

```makefile
XMPPD   = ./xmppd
USERADD = ./useradd

all: $(XMPPD) $(USERADD)

$(XMPPD):
	go build -o $(XMPPD) .

$(USERADD):
	go build -o $(USERADD) ./cmd/useradd

tests: all
	python3 tests/run_all.py

clean:
	rm -f $(XMPPD) $(USERADD) xmppd.log

.PHONY: all tests clean
```

---

## Implementation Order

| Phase | Files | Verifiable |
|-------|-------|------------|
| 1. Foundation | `go.mod`, `xml/node.go`, `xml/decoder.go` | unit test StreamDecoder |
| 2. Storage | `user/user.go`, `roster/roster.go`, `cmd/useradd/main.go` | `./useradd` creates correct data/ |
| 3. Server skeleton | `session/session.go`, `server/server.go`, `main.go` | TCP accept + clean disconnect |
| 4. Stream + Auth | `stream/stream.go`, `stanza/stanza.go` (skeleton), `auth/auth.go` | `test_auth.py` passes |
| 5. Bind + Session | bind/session IQ handlers inside `stanza/stanza.go` | `test_session.py` passes |
| 6. Roster | `roster` IQ handler + Push | `test_roster.py` passes |
| 7. Registration | `register/register.go` | `test_registration.py` passes |
| 8. Presence | `presence/presence.go` | `test_presence.py` passes |
| 9. Message | `message/message.go` | `test_message.py` passes |
| 10. Disco | `disco/disco.go` | `test_disco.py` passes |
| 11. Cleanup | Delete `src/`, `include/`, `tools/`, `build/` | `make tests` all green |

---

## Verification Checklist

- [ ] `make` — builds without errors or warnings
- [ ] `./useradd -d ./data -u alice -p secret` — creates `data/alice/{user.conf,roster.xml,offline/}`
- [ ] `./xmppd -c config/xmppd.conf -D ./data -L DEBUG` — starts, accepts connections, ctrl-C shuts down cleanly
- [ ] `make tests` — all 62 Python scenarios pass

Key regression checks (same as original plan):
- `test_auth.py` test 8: pre-auth IQ → stanza error, connection stays open
- `test_auth.py` test 9: pre-auth message → stream error, connection closes
- `test_message.py` test 5: offline delay stamp matches `\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z`
- `test_registration.py` test 4: on-disk structure verified directly
- `test_presence.py` test 6: both users receive presence broadcast (subscription="both")

---

## What Was NOT Changed

- Package structure (same modules)
- File-on-disk layout (same data/ format)
- CLI flags (same as C version)
- Config file format (same key = value)
- Subscription state machine logic
- Offline message delivery logic
- Implementation order / phases
- No external dependencies
