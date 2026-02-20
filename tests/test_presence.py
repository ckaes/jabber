#!/usr/bin/env python3
"""Tests for presence and subscription management (8 scenarios)."""

import time
from .common import (XMPPConn, check, reset_counters, summary,
                     create_user, delete_user, sasl_plain, DOMAIN)


def _login(username, password, resource='test'):
    """Connect, authenticate, bind. Returns XMPPConn."""
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain(username, password)}</auth>"
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

    delete_user('presuser1')
    delete_user('presuser2')
    create_user('presuser1', 'prespass1')
    create_user('presuser2', 'prespass2')

    # Both users connect and bind
    c1 = _login('presuser1', 'prespass1', resource='r1')
    c2 = _login('presuser2', 'prespass2', resource='r2')

    # ── 1. presuser1 sends available presence (no subscribers yet) ────────────
    print('\n[pres-1] Available presence with no subscribers → no broadcast')
    c1.send('<presence/>')
    c1.recv(timeout=0.3)  # consume any echo
    # presuser2 should NOT receive anything
    resp2 = c2.recv(timeout=0.5)
    check('presuser2 receives nothing (no subscription)', resp2 == '', resp2)

    # ── 2. presuser1 sends subscribe to presuser2 ────────────────────────────
    print('\n[pres-2] presuser1 subscribes to presuser2')
    c1.send(f"<presence type='subscribe' to='presuser2@{DOMAIN}'/>"  )
    # presuser2 should receive the subscribe stanza
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 receives subscribe', 'subscribe' in resp2, resp2)
    check('from presuser1', 'presuser1' in resp2, resp2)
    # presuser1 should receive roster push with ask='subscribe'
    resp1 = c1.recv(timeout=1.0)
    check('presuser1 roster push with ask=subscribe',
          'ask' in resp1 and 'presuser2' in resp1, resp1)

    # ── 3. presuser2 approves (subscribed) ───────────────────────────────────
    print('\n[pres-3] presuser2 sends subscribed → rosters updated')
    c2.send(f"<presence type='subscribed' to='presuser1@{DOMAIN}'/>")
    # presuser1 should receive subscribed notification and/or roster push
    resp1 = c1.recv(timeout=1.0)
    check('presuser1 receives subscribed notification or roster push',
          'subscribed' in resp1 or 'presuser2' in resp1, resp1)
    # Check presuser2's roster (should show subscription=from)
    c2.send("<iq type='get' id='rg1'><query xmlns='jabber:iq:roster'/></iq>")
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 roster shows subscription=from',
          "subscription='from'" in resp2 or 'subscription="from"' in resp2, resp2)

    # ── 4. presuser2 sends subscribe to presuser1 ────────────────────────────
    print('\n[pres-4] presuser2 subscribes to presuser1')
    c2.send(f"<presence type='subscribe' to='presuser1@{DOMAIN}'/>")
    resp1 = c1.recv(timeout=1.0)
    check('presuser1 receives subscribe', 'subscribe' in resp1, resp1)
    check('from presuser2', 'presuser2' in resp1, resp1)

    # ── 5. presuser1 approves (subscribed) → both now 'both' ─────────────────
    print('\n[pres-5] presuser1 sends subscribed → both have subscription=both')
    c1.send(f"<presence type='subscribed' to='presuser2@{DOMAIN}'/>")
    c2.recv(timeout=0.5)  # consume notification
    # Verify presuser1's roster
    c1.send("<iq type='get' id='rg2'><query xmlns='jabber:iq:roster'/></iq>")
    resp1 = c1.recv(timeout=1.0)
    check('presuser1 roster shows subscription=to or both',
          "subscription='to'" in resp1 or 'subscription="to"' in resp1 or
          "subscription='both'" in resp1 or 'subscription="both"' in resp1,
          resp1)
    # Verify presuser2's roster
    c2.send("<iq type='get' id='rg3'><query xmlns='jabber:iq:roster'/></iq>")
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 roster shows subscription=both',
          "subscription='both'" in resp2 or 'subscription="both"' in resp2, resp2)

    # ── 6. presuser1 sends available → presuser2 receives it ─────────────────
    print('\n[pres-6] presuser1 available presence → presuser2 receives broadcast')
    # Clear any pending data from presuser2
    c2.recv(timeout=0.3)
    c1.send('<presence/>')
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 receives available presence', 'presuser1' in resp2, resp2)
    check('not unavailable', 'unavailable' not in resp2, resp2)

    # ── 7. presuser1 sends unavailable → presuser2 receives it ───────────────
    print('\n[pres-7] presuser1 sends unavailable → presuser2 receives it')
    c2.recv(timeout=0.3)
    c1.send("<presence type='unavailable'/>")
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 receives unavailable', 'unavailable' in resp2, resp2)
    check('from presuser1', 'presuser1' in resp2, resp2)

    # ── 8. presuser1 unsubscribes from presuser2 ─────────────────────────────
    print('\n[pres-8] presuser1 unsubscribes from presuser2')
    # presuser1 needs to be available again to trigger unavailable on unsubscribe
    c1.send('<presence/>')
    c2.recv(timeout=0.5)
    c2.recv(timeout=0.3)  # drain
    c1.send(f"<presence type='unsubscribe' to='presuser2@{DOMAIN}'/>")
    resp2 = c2.recv(timeout=1.0)
    check('presuser2 receives unsubscribe notification', 'unsubscribe' in resp2, resp2)
    c1.close()
    c2.close()

    # Teardown
    delete_user('presuser1')
    delete_user('presuser2')

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
