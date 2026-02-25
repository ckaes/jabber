package xml

import (
	"encoding/xml"
	"io"
)

// StreamDecoder wraps encoding/xml.Decoder to produce XMLNode trees from an
// ongoing XMPP stream. Call ReadStanza() repeatedly to get each stanza.
//
// The caller supplies an *bufio.Reader (or any io.ByteReader) so that the
// underlying buffer persists across decoder restarts for stream re-negotiation.
type StreamDecoder struct {
	d *xml.Decoder
}

// NewStreamDecoder creates a StreamDecoder reading from r.
// r should be a *bufio.Reader to avoid double-buffering and to allow safe
// decoder replacement after SASL auth without losing buffered bytes.
func NewStreamDecoder(r io.Reader) *StreamDecoder {
	d := xml.NewDecoder(r)
	d.Strict = false
	return &StreamDecoder{d: d}
}

// ReadStanza reads the next complete stanza from the stream.
//
// Return values:
//   - (node, nil)    — complete stanza or stream-open sentinel
//   - (nil, io.EOF)  — clean stream close or connection EOF
//   - (nil, err)     — XML parse error
//
// Stream open: the returned node satisfies node.IsStreamOpen() == true.
// The stream:stream element is NOT pushed onto the internal stack so depth
// counting is relative to stanza depth, not document depth.
func (sd *StreamDecoder) ReadStanza() (*XMLNode, error) {
	var stack []*XMLNode

	for {
		tok, err := sd.d.Token()
		if err != nil {
			return nil, err
		}

		switch t := tok.(type) {
		case xml.ProcInst:
			// Skip <?xml version='1.0'?> and similar.
			continue

		case xml.StartElement:
			node := startToNode(t)

			if len(stack) == 0 {
				if node.IsStreamOpen() {
					// Stream open — return sentinel without pushing to stack.
					return node, nil
				}
				// First child of stream:stream — start of a new stanza.
			}
			stack = append(stack, node)

		case xml.EndElement:
			if len(stack) == 0 {
				// </stream:stream> — clean close.
				return nil, io.EOF
			}

			// Pop completed node.
			top := stack[len(stack)-1]
			stack = stack[:len(stack)-1]

			if len(stack) == 0 {
				// This was a top-level stanza.
				return top, nil
			}
			// Attach to parent.
			parent := stack[len(stack)-1]
			parent.Children = append(parent.Children, top)

		case xml.CharData:
			if len(stack) > 0 {
				stack[len(stack)-1].Text += string(t)
			}

		case xml.Comment, xml.Directive:
			// ignore
		}
	}
}

func startToNode(t xml.StartElement) *XMLNode {
	node := &XMLNode{
		Name: t.Name.Local,
		NS:   t.Name.Space,
	}
	for _, a := range t.Attr {
		// encoding/xml strips xmlns and xmlns:prefix attributes; those appear
		// as Name.Space on elements instead.  We keep everything else.
		if a.Name.Space == "xmlns" {
			continue
		}
		if a.Name.Space == "" && a.Name.Local == "xmlns" {
			continue
		}
		node.Attrs = append(node.Attrs, XMLAttr{
			Name:  a.Name.Local,
			NS:    a.Name.Space,
			Value: a.Value,
		})
	}
	return node
}
