package stream

import (
	"fmt"
	"jabber/internal/session"
	"jabber/internal/xml"
	"log/slog"
)

// HandleOpen handles a <stream:stream> open element.
// It sends the stream header and appropriate <stream:features>.
func HandleOpen(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	to := node.Attr("to")
	slog.Debug("stream open", "to", to, "jid", s.BareJID())

	if to != "" && to != reg.Domain() {
		slog.Warn("host-unknown", "to", to, "domain", reg.Domain())
		SendError(s, "host-unknown")
		return session.RouteClose
	}

	streamID := xml.GenerateID(16)

	s.Send(fmt.Sprintf(
		"<?xml version='1.0'?>"+
			"<stream:stream from='%s' id='%s' "+
			"xmlns='jabber:client' "+
			"xmlns:stream='http://etherx.jabber.org/streams' "+
			"version='1.0'>",
		reg.Domain(), streamID,
	))

	if s.Authenticated {
		s.Send(
			"<stream:features>" +
				"<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/>" +
				"<session xmlns='urn:ietf:params:xml:ns:xmpp-session'>" +
				"<optional/>" +
				"</session>" +
				"</stream:features>",
		)
	} else {
		s.Send(
			"<stream:features>" +
				"<mechanisms xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>" +
				"<mechanism>PLAIN</mechanism>" +
				"</mechanisms>" +
				"<register xmlns='http://jabber.org/features/iq-register'/>" +
				"</stream:features>",
		)
	}

	s.State = session.StateStreamOpened
	return session.RouteOK
}

// SendError sends a stream-level error and closes the stream.
// The connection should be closed by the caller (return RouteClose).
func SendError(s *session.Session, condition string) {
	s.Send(fmt.Sprintf(
		"<stream:error><%s xmlns='urn:ietf:params:xml:ns:xmpp-streams'/></stream:error></stream:stream>",
		condition,
	))
}
