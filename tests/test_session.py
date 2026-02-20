#!/usr/bin/env python3
"""Tests for stream negotiation, resource binding, and session IQ (8 scenarios)."""

from .common import (XMPPConn, check, reset_counters, summary,
                     create_user, delete_user, sasl_plain, DOMAIN)


def _auth(c, username, password):
    """Authenticate on an already-open stream. Returns auth response."""
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain(username, password)}</auth>"
    )
    return c.recv()


def run():
    reset_counters()

    delete_user('sessuser1')
    create_user('sessuser1', 'sesspass1')

    # ── 1. Pre-auth features: PLAIN + register advertised ─────────────────────
    print('\n[sess-1] Pre-auth features: SASL PLAIN + register present')
    c = XMPPConn()
    features = c.open_stream()
    check('PLAIN mechanism advertised', 'PLAIN' in features, features)
    check('register feature advertised',
          'http://jabber.org/features/iq-register' in features, features)
    c.close()

    # ── 2. Post-auth features: bind + session, no SASL ────────────────────────
    print('\n[sess-2] Post-auth features: bind + session present, SASL absent')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    features = c.open_stream()
    check('bind feature present',
          'urn:ietf:params:xml:ns:xmpp-bind' in features, features)
    check('session feature present',
          'urn:ietf:params:xml:ns:xmpp-session' in features, features)
    check('SASL mechanisms absent', 'xmpp-sasl' not in features, features)
    c.close()

    # ── 3. Stream open with wrong domain → host-unknown ───────────────────────
    print('\n[sess-3] Wrong domain in stream open → host-unknown')
    c = XMPPConn()
    c.send(
        "<?xml version='1.0'?>"
        "<stream:stream to='wrong.domain.example' "
        "xmlns='jabber:client' "
        "xmlns:stream='http://etherx.jabber.org/streams' "
        "version='1.0'>"
    )
    resp = c.recv()
    check('host-unknown stream error', 'host-unknown' in resp, resp)
    c.close()

    # ── 4. Bind with explicit resource ────────────────────────────────────────
    print('\n[sess-4] Bind explicit resource → full JID in result')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    c.open_stream()
    c.send(
        "<iq type='set' id='bind-explicit'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        "<resource>myres</resource></bind></iq>"
    )
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('full JID contains myres',
          f'sessuser1@{DOMAIN}/myres' in resp, resp)
    c.close()

    # ── 5. Bind without <resource> element → auto-generated resource ──────────
    print('\n[sess-5] Bind without <resource> → auto-generated resource in JID')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    c.open_stream()
    c.send(
        "<iq type='set' id='bind-auto'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/></iq>"
    )
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check(f'JID starts with sessuser1@{DOMAIN}/',
          f'sessuser1@{DOMAIN}/' in resp, resp)
    c.close()

    # ── 6. Session establishment IQ ────────────────────────────────────────────
    print('\n[sess-6] Session establishment IQ → result')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    c.open_stream()
    c.send(
        "<iq type='set' id='bind-sess'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        "<resource>sessres</resource></bind></iq>"
    )
    c.recv()  # bind result
    c.send(
        "<iq type='set' id='sess1'>"
        "<session xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>"
    )
    resp = c.recv()
    check('session result IQ',
          ('type="result"' in resp or "type='result'" in resp) and 'sess1' in resp,
          resp)
    c.close()

    # ── 7. Unknown namespace IQ (server-addressed) → service-unavailable ──────
    print('\n[sess-7] Unknown NS IQ to server → service-unavailable')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    c.open_stream()
    c.send(
        "<iq type='set' id='bind-u'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        "<resource>ures</resource></bind></iq>"
    )
    c.recv()
    c.send(
        f"<iq type='get' id='unk1' to='{DOMAIN}'>"
        "<query xmlns='jabber:iq:unknown-ns'/></iq>"
    )
    resp = c.recv()
    check('service-unavailable error', 'service-unavailable' in resp, resp)
    c.close()

    # ── 8. <message> stanza post-auth, pre-bind → not-authorized stream error ─
    print('\n[sess-8] Post-auth pre-bind <message> → not-authorized stream error')
    c = XMPPConn()
    c.open_stream()
    _auth(c, 'sessuser1', 'sesspass1')
    c.open_stream()  # post-auth stream, no bind yet
    c.send(f"<message to='sessuser1@{DOMAIN}'><body>hi</body></message>")
    resp = c.recv()
    check('not-authorized stream error', 'not-authorized' in resp, resp)
    c.close()

    # Teardown
    delete_user('sessuser1')

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
