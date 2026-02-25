package xml

import (
	"crypto/rand"
	"encoding/hex"
	"strings"
)

// XMLAttr holds a single XML attribute.
type XMLAttr struct {
	Name  string
	NS    string
	Value string
}

// XMLNode is an in-memory XML element built by the StreamDecoder.
type XMLNode struct {
	Name     string
	NS       string
	Attrs    []XMLAttr
	Children []*XMLNode
	Text     string
}

// Attr returns the value of the first attribute with the given local name,
// or "" if not present.
func (n *XMLNode) Attr(name string) string {
	for _, a := range n.Attrs {
		if a.Name == name {
			return a.Value
		}
	}
	return ""
}

// FindChild returns the first direct child with the given local name, or nil.
func (n *XMLNode) FindChild(name string) *XMLNode {
	for _, c := range n.Children {
		if c.Name == name {
			return c
		}
	}
	return nil
}

// FindChildNS returns the first direct child with the given local name and
// namespace, or nil.
func (n *XMLNode) FindChildNS(name, ns string) *XMLNode {
	for _, c := range n.Children {
		if c.Name == name && c.NS == ns {
			return c
		}
	}
	return nil
}

// IsStreamOpen returns true if this node represents a <stream:stream> open.
func (n *XMLNode) IsStreamOpen() bool {
	return n.Name == "stream" && n.NS == "http://etherx.jabber.org/streams"
}

// Serialize serializes the node to an XML string suitable for sending over
// an XMPP stream whose default namespace is jabber:client.
func (n *XMLNode) Serialize() string {
	var b strings.Builder
	n.serialize("", &b)
	return b.String()
}

// serialize writes the node into b, tracking parentNS to avoid redundant
// xmlns declarations.
func (n *XMLNode) serialize(parentNS string, b *strings.Builder) {
	b.WriteByte('<')
	b.WriteString(n.Name)

	// Determine effective NS. jabber:client is the stream default — omit it.
	effectiveNS := n.NS
	if effectiveNS == "jabber:client" {
		effectiveNS = ""
	}
	if effectiveNS != "" && effectiveNS != parentNS {
		b.WriteString(" xmlns='")
		b.WriteString(escapeAttr(effectiveNS))
		b.WriteByte('\'')
	}

	for _, a := range n.Attrs {
		b.WriteByte(' ')
		b.WriteString(a.Name)
		b.WriteString("='")
		b.WriteString(escapeAttr(a.Value))
		b.WriteByte('\'')
	}

	if len(n.Children) == 0 && n.Text == "" {
		b.WriteString("/>")
		return
	}

	b.WriteByte('>')
	if n.Text != "" {
		b.WriteString(escapeText(n.Text))
	}
	for _, c := range n.Children {
		c.serialize(effectiveNS, b)
	}
	b.WriteString("</")
	b.WriteString(n.Name)
	b.WriteByte('>')
}

func escapeAttr(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	s = strings.ReplaceAll(s, "'", "&apos;")
	return s
}

func escapeText(s string) string {
	s = strings.ReplaceAll(s, "&", "&amp;")
	s = strings.ReplaceAll(s, "<", "&lt;")
	s = strings.ReplaceAll(s, ">", "&gt;")
	return s
}

// GenerateID returns a random hex string of length n.
func GenerateID(n int) string {
	b := make([]byte, (n+1)/2)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)[:n]
}
