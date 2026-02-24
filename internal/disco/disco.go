package disco

import (
	"fmt"
	"jabber/internal/session"
	"jabber/internal/xml"
	"strings"
)

// HandleInfo responds to disco#info queries.
func HandleInfo(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	id := node.Attr("id")
	fullJID := s.FullJID()

	var b strings.Builder
	fmt.Fprintf(&b, "<iq type='result' from='%s' to='%s'", reg.Domain(), escAttr(fullJID))
	if id != "" {
		fmt.Fprintf(&b, " id='%s'", escAttr(id))
	}
	b.WriteString("><query xmlns='http://jabber.org/protocol/disco#info'>")
	b.WriteString("<identity category='server' type='im' name='xmppd'/>")

	features := []string{
		"http://jabber.org/protocol/disco#info",
		"http://jabber.org/protocol/disco#items",
		"jabber:iq:roster",
		"jabber:iq:register",
		"urn:xmpp:delay",
	}
	for _, f := range features {
		fmt.Fprintf(&b, "<feature var='%s'/>", escAttr(f))
	}
	b.WriteString("</query></iq>")
	s.Send(b.String())
	return session.RouteOK
}

// HandleItems responds to disco#items queries (empty list).
func HandleItems(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	id := node.Attr("id")
	fullJID := s.FullJID()

	var b strings.Builder
	fmt.Fprintf(&b, "<iq type='result' from='%s' to='%s'", reg.Domain(), escAttr(fullJID))
	if id != "" {
		fmt.Fprintf(&b, " id='%s'", escAttr(id))
	}
	b.WriteString("><query xmlns='http://jabber.org/protocol/disco#items'/></iq>")
	s.Send(b.String())
	return session.RouteOK
}

func escAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	return s
}
