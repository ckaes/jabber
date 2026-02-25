package stanza

import (
	"fmt"
	"jabber/internal/auth"
	"jabber/internal/disco"
	"jabber/internal/message"
	"jabber/internal/presence"
	"jabber/internal/register"
	"jabber/internal/roster"
	"jabber/internal/session"
	"jabber/internal/stream"
	"jabber/internal/xml"
	"log/slog"
	"strings"
)

const (
	nsRegister  = "jabber:iq:register"
	nsBind      = "urn:ietf:params:xml:ns:xmpp-bind"
	nsSession   = "urn:ietf:params:xml:ns:xmpp-session"
	nsRoster    = "jabber:iq:roster"
	nsDiscoInfo = "http://jabber.org/protocol/disco#info"
	nsDiscoItems = "http://jabber.org/protocol/disco#items"
)

// Route dispatches a stanza to the appropriate handler.
func Route(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	slog.Debug("stanza", "name", node.Name, "ns", node.NS, "state", s.State, "jid", s.BareJID())

	// Pre-auth: only SASL and in-band registration allowed.
	if s.State == session.StateStreamOpened && !s.Authenticated {
		switch node.Name {
		case "auth":
			return auth.HandleSASL(reg, s, node)
		case "iq":
			child := firstChild(node)
			if child != nil && child.NS == nsRegister {
				return register.HandleIQ(reg, s, node)
			}
			// Non-register IQ: stanza error (connection stays open).
			s.SendStanzaError(node, "cancel", "not-allowed")
			return session.RouteOK
		default:
			// Anything else: stream error (connection closes).
			stream.SendError(s, "not-authorized")
			return session.RouteClose
		}
	}

	// Post-auth routing.
	switch node.Name {
	case "iq":
		return handleIQ(reg, s, node)
	case "message":
		if s.State != session.StateSessionActive && s.State != session.StateBound {
			stream.SendError(s, "not-authorized")
			return session.RouteClose
		}
		return message.Handle(reg, s, node)
	case "presence":
		if s.State != session.StateSessionActive && s.State != session.StateBound {
			stream.SendError(s, "not-authorized")
			return session.RouteClose
		}
		return presence.Handle(reg, s, node)
	default:
		stream.SendError(s, "unsupported-stanza-type")
		return session.RouteClose
	}
}

func handleIQ(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	iqType := node.Attr("type")
	to := node.Attr("to")

	// Route result/error stanzas to their target user.
	if iqType == "result" || iqType == "error" {
		if to != "" && !isServerJID(to, reg.Domain()) {
			targetBare := bareJID(to)
			target := reg.FindByBareJID(targetBare)
			if target != nil {
				setAttr(node, "from", s.FullJID())
				target.Send(node.Serialize())
			}
		}
		return session.RouteOK
	}

	// Dispatch get/set by child element namespace.
	child := firstChild(node)
	childNS := ""
	if child != nil {
		childNS = child.NS
	}

	switch childNS {
	case nsBind:
		return handleBind(reg, s, node)
	case nsSession:
		return handleSessionIQ(s, node)
	case nsRoster:
		if s.State != session.StateSessionActive && s.State != session.StateBound {
			s.SendStanzaError(node, "cancel", "not-allowed")
			return session.RouteOK
		}
		return handleRosterIQ(reg, s, node)
	case nsRegister:
		return register.HandleIQ(reg, s, node)
	case nsDiscoInfo:
		if s.State != session.StateSessionActive && s.State != session.StateBound {
			s.SendStanzaError(node, "cancel", "not-allowed")
			return session.RouteOK
		}
		return disco.HandleInfo(reg, s, node)
	case nsDiscoItems:
		if s.State != session.StateSessionActive && s.State != session.StateBound {
			s.SendStanzaError(node, "cancel", "not-allowed")
			return session.RouteOK
		}
		return disco.HandleItems(reg, s, node)
	default:
		// Unknown namespace: try routing to another user.
		if to != "" && !isServerJID(to, reg.Domain()) &&
			(s.State == session.StateSessionActive || s.State == session.StateBound) {
			targetBare := bareJID(to)
			target := reg.FindByBareJID(targetBare)
			if target != nil {
				setAttr(node, "from", s.FullJID())
				target.Send(node.Serialize())
				return session.RouteOK
			}
		}
		s.SendStanzaError(node, "cancel", "service-unavailable")
		return session.RouteOK
	}
}

// handleBind processes resource binding (RFC 6120 §7).
func handleBind(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	if s.State != session.StateAuthenticated && s.State != session.StateStreamOpened {
		s.SendStanzaError(node, "cancel", "not-allowed")
		return session.RouteOK
	}

	id := node.Attr("id")

	// Extract requested resource.
	resource := ""
	if bindEl := firstChild(node); bindEl != nil {
		if resEl := bindEl.FindChild("resource"); resEl != nil {
			resource = resEl.Text
		}
	}
	if resource == "" {
		resource = xml.GenerateID(8)
	}

	s.JIDResource = resource
	s.State = session.StateBound
	fullJID := s.FullJID()

	// Register under bare JID; kick any conflicting session.
	if old := reg.Register(s); old != nil {
		slog.Info("session conflict — kicking old session", "jid", old.BareJID())
		stream.SendError(old, "conflict")
		old.Close()
	}

	// Send bind result.
	var b strings.Builder
	fmt.Fprintf(&b, "<iq type='result'")
	if id != "" {
		fmt.Fprintf(&b, " id='%s'", escAttr(id))
	}
	b.WriteString("><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>")
	fmt.Fprintf(&b, "<jid>%s</jid>", escText(fullJID))
	b.WriteString("</bind></iq>")
	s.Send(b.String())

	slog.Info("resource bound", "jid", fullJID)
	return session.RouteOK
}

// handleSessionIQ processes session establishment (RFC 3921, deprecated).
func handleSessionIQ(s *session.Session, node *xml.XMLNode) session.RouteResult {
	id := node.Attr("id")
	s.State = session.StateSessionActive

	var b strings.Builder
	b.WriteString("<iq type='result'")
	if id != "" {
		fmt.Fprintf(&b, " id='%s'", escAttr(id))
	}
	b.WriteString("/>")
	s.Send(b.String())

	slog.Info("session established", "jid", s.FullJID())
	return session.RouteOK
}

// handleRosterIQ processes jabber:iq:roster get/set.
func handleRosterIQ(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	iqType := node.Attr("type")
	id := node.Attr("id")

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	switch iqType {
	case "get":
		var b strings.Builder
		fmt.Fprintf(&b, "<iq type='result'")
		if id != "" {
			fmt.Fprintf(&b, " id='%s'", escAttr(id))
		}
		fmt.Fprintf(&b, " to='%s'", escAttr(s.FullJID()))
		b.WriteString("><query xmlns='jabber:iq:roster'>")
		for _, item := range s.Roster.Items {
			fmt.Fprintf(&b, "<item jid='%s'", escAttr(item.JID))
			if item.Name != "" {
				fmt.Fprintf(&b, " name='%s'", escAttr(item.Name))
			}
			fmt.Fprintf(&b, " subscription='%s'", item.Subscription)
			if item.AskSubscribe {
				b.WriteString(" ask='subscribe'")
			}
			b.WriteString("/>")
		}
		b.WriteString("</query></iq>")
		s.Send(b.String())

	case "set":
		queryEl := firstChild(node)
		if queryEl == nil {
			s.SendStanzaError(node, "modify", "bad-request")
			return session.RouteOK
		}
		itemEl := queryEl.FindChild("item")
		if itemEl == nil {
			s.SendStanzaError(node, "modify", "bad-request")
			return session.RouteOK
		}
		jid := itemEl.Attr("jid")
		if jid == "" {
			s.SendStanzaError(node, "modify", "bad-request")
			return session.RouteOK
		}
		itemName := itemEl.Attr("name")
		sub := itemEl.Attr("subscription")

		if sub == "remove" {
			s.Roster.Remove(jid)
			_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)

			// Send result.
			var b strings.Builder
			b.WriteString("<iq type='result'")
			if id != "" {
				fmt.Fprintf(&b, " id='%s'", escAttr(id))
			}
			b.WriteString("/>")
			s.Send(b.String())

			// Roster push with subscription=remove.
			roster.Push(s.Send, s.FullJID(), &roster.Item{
				JID:          jid,
				Subscription: "remove",
			})
		} else {
			// Add or update.
			existing := s.Roster.Find(jid)
			existingSub := "none"
			existingAsk := false
			if existing != nil {
				existingSub = existing.Subscription
				existingAsk = existing.AskSubscribe
			}
			s.Roster.AddOrUpdate(jid, itemName, existingSub, existingAsk)
			if itemName != "" && existing != nil {
				existing.Name = itemName
			}
			_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)

			var b strings.Builder
			b.WriteString("<iq type='result'")
			if id != "" {
				fmt.Fprintf(&b, " id='%s'", escAttr(id))
			}
			b.WriteString("/>")
			s.Send(b.String())

			updated := s.Roster.Find(jid)
			if updated != nil {
				roster.Push(s.Send, s.FullJID(), updated)
			}
		}

	default:
		s.SendStanzaError(node, "cancel", "feature-not-implemented")
	}

	return session.RouteOK
}

// --- helpers ---

func firstChild(n *xml.XMLNode) *xml.XMLNode {
	if len(n.Children) == 0 {
		return nil
	}
	return n.Children[0]
}

func isServerJID(jid, domain string) bool {
	if jid == "" || jid == domain {
		return true
	}
	return false
}

func bareJID(jid string) string {
	if i := strings.Index(jid, "/"); i >= 0 {
		return jid[:i]
	}
	return jid
}

func setAttr(node *xml.XMLNode, name, value string) {
	for i := range node.Attrs {
		if node.Attrs[i].Name == name {
			node.Attrs[i].Value = value
			return
		}
	}
	node.Attrs = append(node.Attrs, xml.XMLAttr{Name: name, Value: value})
}

func escAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	return s
}

func escText(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	s = strings.ReplaceAll(s, ">", "&gt;")
	return s
}
