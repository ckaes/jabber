#!/usr/bin/env python3
"""Tests for SASL PLAIN authentication (9 scenarios)."""

import base64
from .common import (XMPPConn, check, reset_counters, summary,
                     create_user, delete_user, sasl_plain, DOMAIN)


def run():
    reset_counters()

    # Setup — delete first so a failed previous run doesn't leave stale state
    delete_user('authuser1')
    create_user('authuser1', 'authpass1')

    # ── 1. Correct credentials → <success/> ───────────────────────────────────
    print('\n[auth-1] Correct credentials → <success/>')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('authuser1', 'authpass1')}</auth>"
    )
    resp = c.recv()
    check('success received', '<success' in resp, resp)
    c.close()

    # ── 2. Wrong password → <failure><not-authorized/> ────────────────────────
    print('\n[auth-2] Wrong password → <failure><not-authorized/>')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('authuser1', 'wrongpassword')}</auth>"
    )
    resp = c.recv()
    check('failure received', '<failure' in resp, resp)
    check('not-authorized condition', 'not-authorized' in resp, resp)
    c.close()

    # ── 3. Non-existent user → <failure><not-authorized/> ────────────────────
    print('\n[auth-3] Non-existent user → <failure><not-authorized/>')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('nosuchuser', 'anything')}</auth>"
    )
    resp = c.recv()
    check('failure received', '<failure' in resp, resp)
    check('not-authorized condition', 'not-authorized' in resp, resp)
    c.close()

    # ── 4. Unsupported mechanism → <failure><invalid-mechanism/> ─────────────
    print('\n[auth-4] DIGEST-MD5 mechanism → <failure><invalid-mechanism/>')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='DIGEST-MD5'>"
        "dGVzdA==</auth>"
    )
    resp = c.recv()
    check('failure received', '<failure' in resp, resp)
    check('invalid-mechanism condition', 'invalid-mechanism' in resp, resp)
    c.close()

    # ── 5. Empty base64 payload → <failure><not-authorized/> ─────────────────
    print('\n[auth-5] Empty base64 payload → <failure><not-authorized/>')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        "</auth>"
    )
    resp = c.recv()
    check('failure received', '<failure' in resp, resp)
    check('not-authorized condition', 'not-authorized' in resp, resp)
    c.close()

    # ── 6. No password field (only one null separator) → failure ──────────────
    print('\n[auth-6] No password field (single null) → <failure><not-authorized/>')
    c = XMPPConn()
    c.open_stream()
    # encode \0username (no trailing \0password)
    token = b'\x00authuser1'
    bad_payload = base64.b64encode(token).decode()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{bad_payload}</auth>"
    )
    resp = c.recv()
    check('failure received', '<failure' in resp, resp)
    c.close()

    # ── 7. Post-auth features: bind/session present, SASL absent ──────────────
    print('\n[auth-7] Post-auth features: bind/session yes, SASL no')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('authuser1', 'authpass1')}</auth>"
    )
    c.recv()  # success
    features = c.open_stream()
    check('bind feature present',
          'urn:ietf:params:xml:ns:xmpp-bind' in features, features)
    check('SASL mechanisms absent',
          'xmpp-sasl' not in features, features)
    c.close()

    # ── 8. Pre-auth IQ (non-register ns) → stanza error, conn stays open ──────
    print('\n[auth-8] Pre-auth non-register IQ → not-allowed stanza error')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<iq type='get' id='preauth1'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c.recv()
    check('not-allowed error returned', 'not-allowed' in resp, resp)
    check('type=error in response', 'type="error"' in resp or "type='error'" in resp, resp)
    # Connection should still be open — send another stanza
    c.send(
        "<iq type='get' id='preauth2'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp2 = c.recv()
    check('connection still open after stanza error', 'not-allowed' in resp2, resp2)
    c.close()

    # ── 9. Pre-auth <message> → stream error, connection closed ───────────────
    print('\n[auth-9] Pre-auth <message> → not-authorized stream error')
    c = XMPPConn()
    c.open_stream()
    c.send(f"<message to='authuser1@{DOMAIN}'><body>hi</body></message>")
    resp = c.recv()
    check('not-authorized stream error', 'not-authorized' in resp, resp)
    check('stream:error element present', 'stream:error' in resp or 'stream_error' in resp, resp)
    c.close()

    # Teardown
    delete_user('authuser1')

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
