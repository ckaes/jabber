package message

import (
	"fmt"
	"jabber/internal/session"
	"jabber/internal/user"
	"jabber/internal/xml"
	"log/slog"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Handle routes a <message> stanza.
func Handle(reg session.Registry, s *session.Session, node *xml.XMLNode) session.RouteResult {
	to := node.Attr("to")
	msgType := node.Attr("type")
	if msgType == "" {
		msgType = "normal"
	}

	// Parse target JID.
	targetLocal, targetDomain := splitBareJID(to)
	if targetLocal == "" {
		s.SendStanzaError(node, "modify", "jid-malformed")
		return session.RouteOK
	}

	if targetDomain != reg.Domain() {
		s.SendStanzaError(node, "cancel", "item-not-found")
		return session.RouteOK
	}

	if !user.Exists(reg.DataDir(), targetLocal) {
		s.SendStanzaError(node, "cancel", "item-not-found")
		return session.RouteOK
	}

	// Stamp from attribute.
	setAttr(node, "from", s.FullJID())

	targetBare := targetLocal + "@" + targetDomain
	target := reg.FindByBareJID(targetBare)

	if target != nil {
		target.Send(node.Serialize())
	} else if msgType != "error" {
		storeOffline(reg.DataDir(), reg.Domain(), targetLocal, node)
	}

	return session.RouteOK
}

// DeliverOffline sends all stored offline messages to the session,
// then deletes each file. Called on initial presence.
func DeliverOffline(reg session.Registry, s *session.Session) {
	dir := filepath.Join(reg.DataDir(), s.JIDLocal, "offline")
	entries, err := os.ReadDir(dir)
	if err != nil {
		return // no offline directory or empty
	}

	// Collect .xml filenames and sort lexicographically (= numeric order due to zero-padding).
	var files []string
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(e.Name(), ".xml") {
			files = append(files, e.Name())
		}
	}
	sort.Strings(files)

	for _, name := range files {
		path := filepath.Join(dir, name)
		data, err := os.ReadFile(path)
		if err != nil {
			slog.Warn("failed to read offline message", "path", path, "err", err)
			_ = os.Remove(path)
			continue
		}
		s.Send(string(data))
		slog.Info("delivered offline message", "user", s.JIDLocal, "file", name)
		_ = os.Remove(path)
	}
}

func storeOffline(dataDir, domain, username string, node *xml.XMLNode) {
	dir := filepath.Join(dataDir, username, "offline")
	_ = os.MkdirAll(dir, 0755)

	// Determine next sequence number.
	entries, _ := os.ReadDir(dir)
	maxSeq := 0
	for _, e := range entries {
		n := 0
		fmt.Sscanf(e.Name(), "%d.xml", &n)
		if n > maxSeq {
			maxSeq = n
		}
	}

	// Add XEP-0203 delay element.
	stamp := time.Now().UTC().Format("2006-01-02T15:04:05Z")
	delay := &xml.XMLNode{
		Name: "delay",
		NS:   "urn:xmpp:delay",
		Attrs: []xml.XMLAttr{
			{Name: "from", Value: domain},
			{Name: "stamp", Value: stamp},
		},
	}
	node.Children = append(node.Children, delay)

	// Serialize and write.
	data := node.Serialize()
	path := filepath.Join(dir, fmt.Sprintf("%04d.xml", maxSeq+1))
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		slog.Error("failed to write offline message", "path", path, "err", err)
	} else {
		slog.Info("stored offline message", "user", username, "path", path)
	}

	// Remove the delay element we added so the original stanza is not mutated
	// across multiple store calls (defensive).
	node.Children = node.Children[:len(node.Children)-1]
}

// splitBareJID returns (local, domain) from a JID string.
// Returns ("", "") for malformed or server-only JIDs.
func splitBareJID(jid string) (local, domain string) {
	// Strip resource.
	if i := strings.Index(jid, "/"); i >= 0 {
		jid = jid[:i]
	}
	at := strings.Index(jid, "@")
	if at <= 0 {
		return "", jid // no local part
	}
	return jid[:at], jid[at+1:]
}

// setAttr sets or replaces the attribute with the given name on node.
func setAttr(node *xml.XMLNode, name, value string) {
	for i := range node.Attrs {
		if node.Attrs[i].Name == name {
			node.Attrs[i].Value = value
			return
		}
	}
	node.Attrs = append(node.Attrs, xml.XMLAttr{Name: name, Value: value})
}
