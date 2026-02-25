package session

import (
	"bufio"
	"fmt"
	"jabber/internal/roster"
	"jabber/internal/xml"
	"log/slog"
	"net"
	"strings"
	"sync"
	"sync/atomic"
)

// State represents the session negotiation phase.
type State int

const (
	StateConnected    State = iota
	StateStreamOpened       // after first <stream:stream>
	StateAuthenticated      // after SASL success
	StateBound              // after resource bind
	StateSessionActive      // after session IQ
	StateDisconnected
)

// RouteResult signals to sessionLoop what to do after handling a stanza.
type RouteResult int

const (
	RouteOK            RouteResult = iota
	RouteRestartStream             // SASL success: replace XML decoder
	RouteClose                     // stream error sent: close connection
)

// Registry is the minimal server interface used by handler packages.
// Implemented by server.Server; defined here to avoid import cycles.
type Registry interface {
	AllSessions() []*Session
	FindByBareJID(bareJID string) *Session
	Register(s *Session) (old *Session) // returns previous session for conflict kick
	DataDir() string
	Domain() string
}

// Session represents one connected XMPP client.
type Session struct {
	// Identity — set at auth/bind time, then immutable
	JIDLocal    string
	JIDDomain   string
	JIDResource string
	State       State
	Authenticated bool

	// Presence state — written only by this session's goroutine;
	// read by other goroutines for presence broadcast.
	Available           bool
	InitialPresenceSent bool
	PresenceStanza      *xml.XMLNode

	// Roster — loaded lazily, written only by this session's goroutine
	// except for subscription mutations by other sessions (protected by RosterMu).
	Roster   roster.Roster
	RosterMu sync.Mutex

	// I/O
	conn    net.Conn
	br      *bufio.Reader
	Dec     *xml.StreamDecoder

	writeMu  sync.Mutex  // guards conn.Write from any goroutine
	dead     atomic.Bool // set before conn.Close; Send() checks this
	closeOnce sync.Once
}

// New creates a Session for an accepted connection.
func New(conn net.Conn, domain string) *Session {
	br := bufio.NewReader(conn)
	s := &Session{
		JIDDomain: domain,
		conn:      conn,
		br:        br,
	}
	s.Dec = xml.NewStreamDecoder(br)
	return s
}

// BareJID returns "local@domain" or "" if not yet authenticated.
func (s *Session) BareJID() string {
	if s.JIDLocal == "" {
		return ""
	}
	return s.JIDLocal + "@" + s.JIDDomain
}

// FullJID returns "local@domain/resource" or bare JID if not yet bound.
func (s *Session) FullJID() string {
	if s.JIDLocal == "" {
		return ""
	}
	if s.JIDResource == "" {
		return s.BareJID()
	}
	return s.BareJID() + "/" + s.JIDResource
}

// RemoteAddr returns the remote address string for logging.
func (s *Session) RemoteAddr() string {
	if s.conn != nil {
		return s.conn.RemoteAddr().String()
	}
	return "unknown"
}

// Send writes data to the connection. Safe to call from any goroutine.
// Silently drops the write if the session is already dead.
func (s *Session) Send(data string) {
	if s.dead.Load() {
		return
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	if s.dead.Load() {
		return
	}
	if _, err := s.conn.Write([]byte(data)); err != nil {
		slog.Debug("write error", "jid", s.BareJID(), "err", err)
	}
}

// SendNode serializes node and sends it.
func (s *Session) SendNode(node *xml.XMLNode) {
	s.Send(node.Serialize())
}

// SendStanzaError sends an RFC 6120 stanza-level error response.
// The original stanza's tag name and id are preserved; the connection remains open.
func (s *Session) SendStanzaError(original *xml.XMLNode, errType, condition string) {
	var b strings.Builder
	fmt.Fprintf(&b, "<%s type='error'", original.Name)
	if id := original.Attr("id"); id != "" {
		fmt.Fprintf(&b, " id='%s'", escAttr(id))
	}
	fmt.Fprintf(&b, " from='%s'", escAttr(s.JIDDomain))
	if jid := s.FullJID(); jid != "" {
		fmt.Fprintf(&b, " to='%s'", escAttr(jid))
	}
	fmt.Fprintf(&b, "><error type='%s'>", errType)
	fmt.Fprintf(&b, "<%s xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'/>", condition)
	fmt.Fprintf(&b, "</error></%s>", original.Name)
	s.Send(b.String())
}

// RestartDecoder replaces the XML decoder with a fresh one on the same
// bufio.Reader. Called after SASL success; buffered bytes are preserved.
func (s *Session) RestartDecoder() {
	s.Dec = xml.NewStreamDecoder(s.br)
}

// Close marks the session dead and closes the TCP connection.
// Idempotent — safe to call multiple times.
func (s *Session) Close() {
	s.closeOnce.Do(func() {
		s.dead.Store(true)
		s.conn.Close()
	})
}

func escAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	return s
}
