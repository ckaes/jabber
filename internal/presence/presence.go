package presence

import (
	"fmt"
	"jabber/internal/message"
	"jabber/internal/roster"
	"jabber/internal/session"
	"jabber/internal/xml"
	"log/slog"
	"strings"
)

// OnInitialPresence is called when a session sends its first available presence.
// Set by server package init to wire in message delivery without circular imports.
// (presence → message is fine; the alternative hook approach is kept for future extensibility.)
var OnInitialPresence func(reg session.Registry, s *session.Session)

// Handle dispatches presence stanzas to the appropriate sub-handler.
func Handle(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	ptype := node.Attr("type")
	to := node.Attr("to")

	switch ptype {
	case "":
		handleAvailable(reg, s, node)
	case "unavailable":
		handleUnavailable(reg, s)
	case "subscribe":
		handleSubscribe(reg, s, to)
	case "subscribed":
		handleSubscribed(reg, s, to)
	case "unsubscribe":
		handleUnsubscribe(reg, s, to)
	case "unsubscribed":
		handleUnsubscribed(reg, s, to)
	default:
		slog.Warn("unknown presence type", "type", ptype, "jid", s.BareJID())
	}
	return session.RouteOK
}

// BroadcastUnavailable sends <presence type='unavailable'> to all subscribed
// contacts. Called from sessionLoop's defer after Unregister().
func BroadcastUnavailable(reg session.Registry, s *session.Session) {
	if !s.Available && !s.InitialPresenceSent {
		return
	}
	if s.JIDLocal == "" {
		return
	}

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	unavail := fmt.Sprintf("<presence type='unavailable' from='%s'/>", escAttr(s.FullJID()))

	for _, item := range s.Roster.Items {
		if !roster.SubHasFrom(item.Subscription) {
			continue
		}
		bare := bareJID(item.JID)
		contact := reg.FindByBareJID(bare)
		if contact != nil && contact != s {
			contact.Send(unavail)
		}
	}
	s.Available = false
}

// --- Available presence ---

func handleAvailable(reg session.Registry, s *session.Session, node *xml.XMLNode) {
	isInitial := !s.Available
	s.Available = true

	// Clone the stanza and stamp our full JID as 'from'.
	pres := clonePresence(node, s.FullJID())
	s.PresenceStanza = pres

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	presXML := pres.Serialize()

	// Broadcast our presence to contacts who have 'from' or 'both' subscription.
	for _, item := range s.Roster.Items {
		if !roster.SubHasFrom(item.Subscription) {
			continue
		}
		bare := bareJID(item.JID)
		contact := reg.FindByBareJID(bare)
		if contact != nil {
			contact.Send(presXML)
		}
	}

	// Receive current presence from contacts we subscribe to.
	for _, item := range s.Roster.Items {
		if !roster.SubHasTo(item.Subscription) {
			continue
		}
		bare := bareJID(item.JID)
		contact := reg.FindByBareJID(bare)
		if contact != nil && contact.Available && contact.PresenceStanza != nil {
			s.Send(contact.PresenceStanza.Serialize())
		}
	}

	if isInitial {
		s.InitialPresenceSent = true
		message.DeliverOffline(reg, s)
		RedeliverPendingSubscribes(reg, s)
	}
}

// --- Unavailable presence ---

func handleUnavailable(reg session.Registry, s *session.Session) {
	BroadcastUnavailable(reg, s)
}

// --- Subscribe ---

func handleSubscribe(reg session.Registry, s *session.Session, to string) {
	targetBare := bareJID(to)
	senderBare := s.BareJID()

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	// Ensure sender's roster has entry for target with ask=true.
	item := s.Roster.Find(targetBare)
	if item == nil {
		s.Roster.AddOrUpdate(targetBare, "", "none", true)
		item = s.Roster.Find(targetBare)
	} else {
		item.AskSubscribe = true
	}
	_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)
	roster.Push(s.Send, s.FullJID(), item)

	// Deliver to target if online.
	target := reg.FindByBareJID(targetBare)
	if target != nil {
		target.Send(fmt.Sprintf(
			"<presence type='subscribe' from='%s' to='%s'/>",
			escAttr(senderBare), escAttr(targetBare),
		))
	}
}

// --- Subscribed ---

func handleSubscribed(reg session.Registry, s *session.Session, to string) {
	targetBare := bareJID(to)
	senderBare := s.BareJID()

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	// Update sender's (approver's) roster: none→from, to→both.
	senderItem := s.Roster.Find(targetBare)
	if senderItem == nil {
		s.Roster.AddOrUpdate(targetBare, "", "from", false)
		senderItem = s.Roster.Find(targetBare)
	} else {
		switch senderItem.Subscription {
		case "none":
			senderItem.Subscription = "from"
		case "to":
			senderItem.Subscription = "both"
		}
	}
	_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)
	if senderItem != nil {
		roster.Push(s.Send, s.FullJID(), senderItem)
	}

	// Update requester's roster: none→to, from→both; clear ask.
	targetSession := reg.FindByBareJID(targetBare)
	targetLocal := localPart(targetBare)

	if targetSession != nil {
		targetSession.RosterMu.Lock()
		if !targetSession.Roster.Loaded {
			targetSession.Roster = roster.Load(reg.DataDir(), targetLocal)
		}
		targetItem := targetSession.Roster.Find(senderBare)
		if targetItem != nil {
			switch targetItem.Subscription {
			case "none":
				targetItem.Subscription = "to"
			case "from":
				targetItem.Subscription = "both"
			}
			targetItem.AskSubscribe = false
		}
		_ = roster.Save(reg.DataDir(), targetLocal, &targetSession.Roster)
		if targetItem != nil {
			roster.Push(targetSession.Send, targetSession.FullJID(), targetItem)
		}
		targetSession.RosterMu.Unlock()

		// Send sender's current presence to target.
		if s.Available && s.PresenceStanza != nil {
			targetSession.Send(s.PresenceStanza.Serialize())
		}
		// Send subscribed notification.
		targetSession.Send(fmt.Sprintf(
			"<presence type='subscribed' from='%s' to='%s'/>",
			escAttr(senderBare), escAttr(targetBare),
		))
	} else {
		// Target offline: modify roster on disk.
		modifyDiskRoster(reg.DataDir(), targetLocal, func(r *roster.Roster) {
			item := r.Find(senderBare)
			if item != nil {
				switch item.Subscription {
				case "none":
					item.Subscription = "to"
				case "from":
					item.Subscription = "both"
				}
				item.AskSubscribe = false
			}
		})
	}
}

// --- Unsubscribe ---

func handleUnsubscribe(reg session.Registry, s *session.Session, to string) {
	targetBare := bareJID(to)
	senderBare := s.BareJID()

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	// Update sender's roster: to→none, both→from; clear ask.
	senderItem := s.Roster.Find(targetBare)
	if senderItem != nil {
		switch senderItem.Subscription {
		case "to":
			senderItem.Subscription = "none"
		case "both":
			senderItem.Subscription = "from"
		}
		senderItem.AskSubscribe = false
		_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)
		roster.Push(s.Send, s.FullJID(), senderItem)
	}

	// Update target's roster: from→none, both→to.
	targetSession := reg.FindByBareJID(targetBare)
	targetLocal := localPart(targetBare)

	if targetSession != nil {
		targetSession.RosterMu.Lock()
		if !targetSession.Roster.Loaded {
			targetSession.Roster = roster.Load(reg.DataDir(), targetLocal)
		}
		targetItem := targetSession.Roster.Find(senderBare)
		if targetItem != nil {
			switch targetItem.Subscription {
			case "from":
				targetItem.Subscription = "none"
			case "both":
				targetItem.Subscription = "to"
			}
			_ = roster.Save(reg.DataDir(), targetLocal, &targetSession.Roster)
			roster.Push(targetSession.Send, targetSession.FullJID(), targetItem)
		}
		targetSession.RosterMu.Unlock()

		// Deliver unsubscribe notification.
		targetSession.Send(fmt.Sprintf(
			"<presence type='unsubscribe' from='%s' to='%s'/>",
			escAttr(senderBare), escAttr(targetBare),
		))
		// Send unavailable from sender if they're available.
		if s.Available {
			targetSession.Send(fmt.Sprintf(
				"<presence type='unavailable' from='%s'/>",
				escAttr(s.FullJID()),
			))
		}
	} else {
		modifyDiskRoster(reg.DataDir(), targetLocal, func(r *roster.Roster) {
			item := r.Find(senderBare)
			if item != nil {
				switch item.Subscription {
				case "from":
					item.Subscription = "none"
				case "both":
					item.Subscription = "to"
				}
			}
		})
	}
}

// --- Unsubscribed ---

func handleUnsubscribed(reg session.Registry, s *session.Session, to string) {
	targetBare := bareJID(to)
	senderBare := s.BareJID()

	if !s.Roster.Loaded {
		s.Roster = roster.Load(reg.DataDir(), s.JIDLocal)
	}

	// Update sender's roster: from→none, both→to.
	senderItem := s.Roster.Find(targetBare)
	if senderItem != nil {
		switch senderItem.Subscription {
		case "from":
			senderItem.Subscription = "none"
		case "both":
			senderItem.Subscription = "to"
		}
		_ = roster.Save(reg.DataDir(), s.JIDLocal, &s.Roster)
		roster.Push(s.Send, s.FullJID(), senderItem)
	}

	// Update target's roster: to→none, both→from; clear ask.
	targetSession := reg.FindByBareJID(targetBare)
	targetLocal := localPart(targetBare)

	if targetSession != nil {
		targetSession.RosterMu.Lock()
		if !targetSession.Roster.Loaded {
			targetSession.Roster = roster.Load(reg.DataDir(), targetLocal)
		}
		targetItem := targetSession.Roster.Find(senderBare)
		if targetItem != nil {
			switch targetItem.Subscription {
			case "to":
				targetItem.Subscription = "none"
			case "both":
				targetItem.Subscription = "from"
			}
			targetItem.AskSubscribe = false
			_ = roster.Save(reg.DataDir(), targetLocal, &targetSession.Roster)
			roster.Push(targetSession.Send, targetSession.FullJID(), targetItem)
		}
		targetSession.RosterMu.Unlock()

		// Deliver unsubscribed notification.
		targetSession.Send(fmt.Sprintf(
			"<presence type='unsubscribed' from='%s' to='%s'/>",
			escAttr(senderBare), escAttr(targetBare),
		))
		// Send unavailable from sender if they're available.
		if s.Available {
			targetSession.Send(fmt.Sprintf(
				"<presence type='unavailable' from='%s'/>",
				escAttr(s.FullJID()),
			))
		}
	} else {
		modifyDiskRoster(reg.DataDir(), targetLocal, func(r *roster.Roster) {
			item := r.Find(senderBare)
			if item != nil {
				switch item.Subscription {
				case "to":
					item.Subscription = "none"
				case "both":
					item.Subscription = "from"
				}
				item.AskSubscribe = false
			}
		})
	}
}

// RedeliverPendingSubscribes scans all online sessions and re-delivers any
// pending subscribe requests aimed at s. Called on initial presence.
func RedeliverPendingSubscribes(reg session.Registry, s *session.Session) {
	ourBare := s.BareJID()
	for _, other := range reg.AllSessions() {
		if other == s || other.JIDLocal == "" {
			continue
		}
		if !other.Roster.Loaded {
			continue
		}
		for _, item := range other.Roster.Items {
			if !item.AskSubscribe {
				continue
			}
			if bareJID(item.JID) != ourBare {
				continue
			}
			s.Send(fmt.Sprintf(
				"<presence type='subscribe' from='%s' to='%s'/>",
				escAttr(other.BareJID()), escAttr(ourBare),
			))
		}
	}
}

// --- helpers ---

func clonePresence(node *xml.XMLNode, fromJID string) *xml.XMLNode {
	clone := &xml.XMLNode{Name: node.Name, NS: node.NS}
	for _, a := range node.Attrs {
		if a.Name != "from" {
			clone.Attrs = append(clone.Attrs, a)
		}
	}
	clone.Attrs = append(clone.Attrs, xml.XMLAttr{Name: "from", Value: fromJID})
	clone.Children = node.Children
	clone.Text = node.Text
	return clone
}

// modifyDiskRoster loads a user's roster file, applies fn, and saves it.
func modifyDiskRoster(dataDir, username string, fn func(*roster.Roster)) {
	r := roster.Load(dataDir, username)
	fn(&r)
	_ = roster.Save(dataDir, username, &r)
}

// bareJID returns just "local@domain" from a JID that may include a resource.
func bareJID(jid string) string {
	if i := strings.Index(jid, "/"); i >= 0 {
		return jid[:i]
	}
	return jid
}

// localPart returns the part before '@'.
func localPart(jid string) string {
	if i := strings.Index(jid, "@"); i >= 0 {
		return jid[:i]
	}
	return jid
}

func escAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	return s
}
