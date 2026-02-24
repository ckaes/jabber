package server

import (
	"context"
	"jabber/internal/presence"
	"jabber/internal/session"
	"jabber/internal/stanza"
	"jabber/internal/stream"
	"log/slog"
	"net"
	"sync"
)

// Server manages the TCP listener and all active sessions.
type Server struct {
	domain  string
	dataDir string
	addr    string

	mu        sync.RWMutex
	byBareJID map[string]*session.Session
	all       []*session.Session

	fileMu sync.Mutex // serializes offline-user roster/file writes
}

// New creates a Server with the given configuration.
func New(domain, dataDir, addr string) *Server {
	return &Server{
		domain:    domain,
		dataDir:   dataDir,
		addr:      addr,
		byBareJID: make(map[string]*session.Session),
	}
}

// Run starts the TCP listener and accepts connections until ctx is cancelled.
func (srv *Server) Run(ctx context.Context) error {
	ln, err := net.Listen("tcp", srv.addr)
	if err != nil {
		return err
	}

	go func() {
		<-ctx.Done()
		ln.Close()
	}()

	slog.Info("listening", "addr", srv.addr, "domain", srv.domain)

	for {
		conn, err := ln.Accept()
		if err != nil {
			if ctx.Err() != nil {
				return nil // graceful shutdown
			}
			slog.Error("accept error", "err", err)
			continue
		}

		s := session.New(conn, srv.domain)
		srv.addToAll(s)
		go srv.sessionLoop(s)
	}
}

// sessionLoop is the per-session goroutine. It reads stanzas and routes them.
func (srv *Server) sessionLoop(s *session.Session) {
	defer func() {
		srv.Unregister(s)
		presence.BroadcastUnavailable(srv, s)
		s.Close()
		slog.Info("session ended", "jid", s.BareJID(), "addr", s.RemoteAddr())
	}()

	slog.Info("client connected", "addr", s.RemoteAddr())

	for {
		node, err := s.Dec.ReadStanza()
		if err != nil {
			if err.Error() != "EOF" {
				slog.Debug("read error", "jid", s.BareJID(), "err", err)
			}
			return
		}

		var result session.RouteResult
		if node.IsStreamOpen() {
			result = stream.HandleOpen(srv, s, node)
		} else {
			result = stanza.Route(srv, s, node)
		}

		switch result {
		case session.RouteRestartStream:
			s.RestartDecoder()
		case session.RouteClose:
			return
		}

		// After bind completes, deliver offline messages and pending subscribes
		// the first time the session becomes session-active via presence.
		// (Actual delivery is triggered by initial presence in presence.Handle.)
	}
}

// Register adds a newly-bound session under its bare JID.
// Returns the previous session (if any) so the caller can kick it.
func (srv *Server) Register(s *session.Session) *session.Session {
	bareJID := s.BareJID()
	srv.mu.Lock()
	old := srv.byBareJID[bareJID]
	srv.byBareJID[bareJID] = s
	srv.mu.Unlock()
	return old
}

// Unregister removes a session from all indices.
// Only deletes byBareJID[jid] if it still points to s — handles the
// kick-then-unregister race where a new session is already registered.
func (srv *Server) Unregister(s *session.Session) {
	srv.mu.Lock()
	defer srv.mu.Unlock()

	bareJID := s.BareJID()
	if bareJID != "" && srv.byBareJID[bareJID] == s {
		delete(srv.byBareJID, bareJID)
	}

	for i, sess := range srv.all {
		if sess == s {
			srv.all[i] = srv.all[len(srv.all)-1]
			srv.all = srv.all[:len(srv.all)-1]
			break
		}
	}
}

func (srv *Server) addToAll(s *session.Session) {
	srv.mu.Lock()
	srv.all = append(srv.all, s)
	srv.mu.Unlock()
}

// --- session.Registry interface implementation ---

// FindByBareJID returns the live session for a bare JID, or nil.
func (srv *Server) FindByBareJID(bareJID string) *session.Session {
	srv.mu.RLock()
	defer srv.mu.RUnlock()
	return srv.byBareJID[bareJID]
}

// AllSessions returns a snapshot copy of all connected sessions.
func (srv *Server) AllSessions() []*session.Session {
	srv.mu.RLock()
	defer srv.mu.RUnlock()
	result := make([]*session.Session, len(srv.all))
	copy(result, srv.all)
	return result
}

// DataDir returns the configured data directory path.
func (srv *Server) DataDir() string { return srv.dataDir }

// Domain returns the XMPP domain name.
func (srv *Server) Domain() string { return srv.domain }

// WithFileMu serialises disk operations for offline users.
func (srv *Server) WithFileMu(fn func()) {
	srv.fileMu.Lock()
	defer srv.fileMu.Unlock()
	fn()
}

// Ensure Server implements session.Registry (compile-time check).
var _ session.Registry = (*Server)(nil)
