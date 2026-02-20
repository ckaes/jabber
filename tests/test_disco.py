#!/usr/bin/env python3
"""Tests for XEP-0030 service discovery (4 scenarios)."""

from .common import (XMPPConn, check, reset_counters, summary,
                     sasl_plain, DOMAIN)

# Uses alice/secret — existing user, no roster modifications needed.
_USER = 'alice'
_PASS = 'secret'

EXPECTED_FEATURES = [
    'http://jabber.org/protocol/disco#info',
    'http://jabber.org/protocol/disco#items',
    'jabber:iq:roster',
    'jabber:iq:register',
    'urn:xmpp:delay',
]


def _login(resource='test'):
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain(_USER, _PASS)}</auth>"
    )
    c.recv()
    c.open_stream()
    c.send(
        f"<iq type='set' id='bind1'>"
        f"<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        f"<resource>{resource}</resource></bind></iq>"
    )
    c.recv()
    return c


def run():
    reset_counters()

    # ── 1. disco#info — all 5 features present ───────────────────────────────
    print('\n[disco-1] disco#info includes all 5 required features')
    c = _login(resource='disco1')
    c.send(
        f"<iq type='get' id='d1' to='{DOMAIN}'>"
        "<query xmlns='http://jabber.org/protocol/disco#info'/></iq>"
    )
    resp = c.recv()
    for feat in EXPECTED_FEATURES:
        check(f'feature {feat}', feat in resp, resp)
    c.close()

    # ── 2. disco#info identity ────────────────────────────────────────────────
    print('\n[disco-2] disco#info identity attributes')
    c = _login(resource='disco2')
    c.send(
        f"<iq type='get' id='d2' to='{DOMAIN}'>"
        "<query xmlns='http://jabber.org/protocol/disco#info'/></iq>"
    )
    resp = c.recv()
    check("category='server'",
          "category='server'" in resp or 'category="server"' in resp, resp)
    check("type='im'",
          "type='im'" in resp or 'type="im"' in resp, resp)
    check("name='xmppd'",
          "name='xmppd'" in resp or 'name="xmppd"' in resp, resp)
    c.close()

    # ── 3. disco#items — empty items list ────────────────────────────────────
    print('\n[disco-3] disco#items returns empty items list')
    c = _login(resource='disco3')
    c.send(
        f"<iq type='get' id='d3' to='{DOMAIN}'>"
        "<query xmlns='http://jabber.org/protocol/disco#items'/></iq>"
    )
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('disco#items namespace present',
          'disco#items' in resp, resp)
    check('no <item> children', '<item' not in resp, resp)
    c.close()

    # ── 4. disco#info before binding (post-auth, pre-bind) → not-allowed ──────
    print('\n[disco-4] disco#info before binding → not-allowed error')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain(_USER, _PASS)}</auth>"
    )
    c.recv()
    c.open_stream()
    # Send disco#info WITHOUT binding first
    c.send(
        f"<iq type='get' id='d4' to='{DOMAIN}'>"
        "<query xmlns='http://jabber.org/protocol/disco#info'/></iq>"
    )
    resp = c.recv()
    check('not-allowed error', 'not-allowed' in resp, resp)
    c.close()

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
