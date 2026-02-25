package register

import (
	"jabber/internal/session"
	"jabber/internal/user"
	"jabber/internal/xml"
	"fmt"
	"log/slog"
)

const regNS = "jabber:iq:register"

// HandleIQ handles registration IQ stanzas (XEP-0077).
func HandleIQ(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	iqType := node.Attr("type")
	id := node.Attr("id")

	switch iqType {
	case "get":
		return handleGet(s, id)
	case "set":
		return handleSet(reg, s, node, id)
	default:
		s.SendStanzaError(node, "cancel", "bad-request")
		return session.RouteOK
	}
}

func handleGet(s *session.Session, id string) session.RouteResult {
	result := buildResultIQ(s, id, false)
	result += "<query xmlns='jabber:iq:register'>" +
		"<instructions>Choose a username and password.</instructions>" +
		"<username/><password/>" +
		"</query></iq>"
	s.Send(result)
	return session.RouteOK
}

func handleSet(reg session.Registry, s *session.Session, node *xml.XMLNode, id string) session.RouteResult {
	query := firstChild(node)
	if query == nil {
		s.SendStanzaError(node, "modify", "bad-request")
		return session.RouteOK
	}

	// Check for <remove/>
	if query.FindChild("remove") != nil {
		if !s.Authenticated {
			s.SendStanzaError(node, "cancel", "not-allowed")
			return session.RouteOK
		}
		// Send result first, then delete and close.
		s.Send(buildResultIQ(s, id, true) + "</iq>")
		username := s.JIDLocal
		if err := user.Delete(reg.DataDir(), username); err != nil {
			slog.Error("user delete failed", "user", username, "err", err)
		} else {
			slog.Info("account deleted", "user", username)
		}
		return session.RouteClose
	}

	// Extract username and password children.
	uname := childText(query, "username")
	pw := childText(query, "password")

	if uname == "" || pw == "" {
		s.SendStanzaError(node, "modify", "bad-request")
		return session.RouteOK
	}

	if !s.Authenticated {
		// Pre-auth: create new account.
		err := user.Create(reg.DataDir(), uname, pw)
		if err != nil {
			switch err.Error() {
			case "conflict":
				s.SendStanzaError(node, "cancel", "conflict")
			case "invalid-username":
				s.SendStanzaError(node, "modify", "not-acceptable")
			default:
				s.SendStanzaError(node, "wait", "internal-server-error")
			}
			return session.RouteOK
		}
		slog.Info("new account registered", "user", uname)
		s.Send(buildResultIQ(s, id, false) + "</iq>")
		return session.RouteOK
	}

	// Post-auth: password change; username must match.
	if uname != s.JIDLocal {
		s.SendStanzaError(node, "cancel", "not-allowed")
		return session.RouteOK
	}
	if err := user.ChangePassword(reg.DataDir(), uname, pw); err != nil {
		s.SendStanzaError(node, "wait", "internal-server-error")
		return session.RouteOK
	}
	slog.Info("password changed", "user", uname)
	s.Send(buildResultIQ(s, id, true) + "</iq>")
	return session.RouteOK
}

// buildResultIQ returns an opening <iq type='result' ...> tag (without closing).
// If includeTo is true, a to= attribute with the session's full JID is added.
func buildResultIQ(s *session.Session, id string, includeTo bool) string {
	r := "<iq type='result'"
	if id != "" {
		r += fmt.Sprintf(" id='%s'", id)
	}
	r += fmt.Sprintf(" from='%s'", s.JIDDomain)
	if includeTo {
		if jid := s.FullJID(); jid != "" {
			r += fmt.Sprintf(" to='%s'", jid)
		}
	}
	return r
}

func firstChild(n *xml.XMLNode) *xml.XMLNode {
	for _, c := range n.Children {
		return c
	}
	return nil
}

func childText(n *xml.XMLNode, name string) string {
	if c := n.FindChild(name); c != nil {
		return c.Text
	}
	return ""
}
