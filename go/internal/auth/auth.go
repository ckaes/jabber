package auth

import (
	"encoding/base64"
	"jabber/internal/session"
	"jabber/internal/user"
	"jabber/internal/xml"
	"log/slog"
	"strings"
)

const saslNS = "urn:ietf:params:xml:ns:xmpp-sasl"

// HandleSASL handles a <auth> element under the SASL namespace.
func HandleSASL(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	mechanism := node.Attr("mechanism")
	if !strings.EqualFold(mechanism, "PLAIN") {
		slog.Warn("unsupported SASL mechanism", "mechanism", mechanism, "addr", s.RemoteAddr())
		s.Send("<failure xmlns='" + saslNS + "'><invalid-mechanism/></failure>")
		return session.RouteOK
	}

	payload := strings.TrimSpace(node.Text)
	if payload == "" {
		slog.Warn("empty SASL PLAIN payload", "addr", s.RemoteAddr())
		s.Send("<failure xmlns='" + saslNS + "'><not-authorized/></failure>")
		return session.RouteOK
	}

	decoded, err := base64.StdEncoding.DecodeString(payload)
	if err != nil {
		slog.Warn("bad base64 in SASL PLAIN", "addr", s.RemoteAddr())
		s.Send("<failure xmlns='" + saslNS + "'><not-authorized/></failure>")
		return session.RouteOK
	}

	// SASL PLAIN format: [authzid]\0authcid\0passwd
	authcid, passwd, ok := parseSASLPlain(decoded)
	if !ok {
		slog.Warn("malformed SASL PLAIN payload", "addr", s.RemoteAddr())
		s.Send("<failure xmlns='" + saslNS + "'><not-authorized/></failure>")
		return session.RouteOK
	}

	slog.Debug("SASL PLAIN auth attempt", "user", authcid)

	if !user.CheckPassword(reg.DataDir(), authcid, passwd) {
		slog.Info("auth failed", "user", authcid, "addr", s.RemoteAddr())
		s.Send("<failure xmlns='" + saslNS + "'><not-authorized/></failure>")
		return session.RouteOK
	}

	slog.Info("authenticated", "user", authcid, "addr", s.RemoteAddr())
	s.JIDLocal = authcid
	s.Authenticated = true
	s.State = session.StateAuthenticated
	s.Send("<success xmlns='" + saslNS + "'/>")

	// Signal sessionLoop to replace the XML decoder.
	return session.RouteRestartStream
}

// parseSASLPlain splits [authzid]\0authcid\0passwd.
func parseSASLPlain(b []byte) (authcid, passwd string, ok bool) {
	// Skip optional authzid (before first \0).
	i := 0
	for i < len(b) && b[i] != 0 {
		i++
	}
	i++ // skip first \0
	if i >= len(b) {
		return "", "", false
	}

	// authcid runs to next \0.
	j := i
	for j < len(b) && b[j] != 0 {
		j++
	}
	authcid = string(b[i:j])
	j++ // skip second \0
	if j > len(b) {
		return "", "", false
	}
	passwd = string(b[j:])
	if authcid == "" || passwd == "" {
		return "", "", false
	}
	return authcid, passwd, true
}
