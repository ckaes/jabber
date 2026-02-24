package roster

import (
	"encoding/xml"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Item holds one contact in a roster.
type Item struct {
	JID          string
	Name         string
	Subscription string // "none", "to", "from", "both"
	AskSubscribe bool   // pending outbound subscribe
}

// Roster is the in-memory contact list for one user.
type Roster struct {
	Items  []Item
	Loaded bool
}

// SubHasFrom returns true if subscription includes the 'from' direction.
func SubHasFrom(sub string) bool { return sub == "from" || sub == "both" }

// SubHasTo returns true if subscription includes the 'to' direction.
func SubHasTo(sub string) bool { return sub == "to" || sub == "both" }

// Load reads and parses data/<username>/roster.xml.
// A missing file is treated as an empty roster (not an error).
func Load(dataDir, username string) Roster {
	path := filepath.Join(dataDir, username, "roster.xml")
	data, err := os.ReadFile(path)
	if err != nil {
		return Roster{Loaded: true}
	}

	type xmlItem struct {
		JID  string `xml:"jid,attr"`
		Name string `xml:"name,attr"`
		Sub  string `xml:"subscription,attr"`
		Ask  string `xml:"ask,attr"`
	}
	type xmlRoster struct {
		XMLName xml.Name  `xml:"roster"`
		Items   []xmlItem `xml:"item"`
	}

	var xr xmlRoster
	if err := xml.Unmarshal(data, &xr); err != nil {
		return Roster{Loaded: true}
	}

	r := Roster{Loaded: true}
	for _, xi := range xr.Items {
		sub := xi.Sub
		if sub == "" {
			sub = "none"
		}
		r.Items = append(r.Items, Item{
			JID:          xi.JID,
			Name:         xi.Name,
			Subscription: sub,
			AskSubscribe: xi.Ask == "subscribe",
		})
	}
	return r
}

// Save writes the roster to data/<username>/roster.xml.
func Save(dataDir, username string, r *Roster) error {
	var b strings.Builder
	b.WriteString("<?xml version=\"1.0\"?>\n<roster>\n")
	for _, item := range r.Items {
		fmt.Fprintf(&b, "  <item jid=%q", item.JID)
		if item.Name != "" {
			fmt.Fprintf(&b, " name=%q", item.Name)
		}
		fmt.Fprintf(&b, " subscription=%q", item.Subscription)
		if item.AskSubscribe {
			b.WriteString(` ask="subscribe"`)
		}
		b.WriteString("/>\n")
	}
	b.WriteString("</roster>\n")

	path := filepath.Join(dataDir, username, "roster.xml")
	return os.WriteFile(path, []byte(b.String()), 0644)
}

// Find returns a pointer to the item with the given bare JID, or nil.
func (r *Roster) Find(jid string) *Item {
	for i := range r.Items {
		if r.Items[i].JID == jid {
			return &r.Items[i]
		}
	}
	return nil
}

// AddOrUpdate inserts or updates an item.
func (r *Roster) AddOrUpdate(jid, name, subscription string, ask bool) {
	if item := r.Find(jid); item != nil {
		if name != "" {
			item.Name = name
		}
		if subscription != "" {
			item.Subscription = subscription
		}
		item.AskSubscribe = ask
		return
	}
	sub := subscription
	if sub == "" {
		sub = "none"
	}
	r.Items = append(r.Items, Item{
		JID:          jid,
		Name:         name,
		Subscription: sub,
		AskSubscribe: ask,
	})
}

// Remove removes the item with the given JID. Returns true if found.
func (r *Roster) Remove(jid string) bool {
	for i, item := range r.Items {
		if item.JID == jid {
			r.Items = append(r.Items[:i], r.Items[i+1:]...)
			return true
		}
	}
	return false
}

// Push sends a roster-push IQ to the given session via the send function.
// send is typically session.Send; fullJID is the session's full JID.
func Push(send func(string), fullJID string, item *Item) {
	id := pushID()
	var b strings.Builder
	fmt.Fprintf(&b, "<iq type='set' id='%s' to='%s'>", id, escAttr(fullJID))
	b.WriteString("<query xmlns='jabber:iq:roster'>")
	fmt.Fprintf(&b, "<item jid='%s'", escAttr(item.JID))
	if item.Name != "" {
		fmt.Fprintf(&b, " name='%s'", escAttr(item.Name))
	}
	fmt.Fprintf(&b, " subscription='%s'", item.Subscription)
	if item.AskSubscribe {
		b.WriteString(" ask='subscribe'")
	}
	b.WriteString("/></query></iq>")
	send(b.String())
}

// pushID generates a short random ID for roster-push stanzas.
// Uses math/rand for simplicity since these don't require cryptographic quality.
var pushCounter uint64

func pushID() string {
	pushCounter++
	return fmt.Sprintf("rp%d", pushCounter)
}

func escAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	return s
}
