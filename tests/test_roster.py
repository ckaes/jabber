#!/usr/bin/env python3
"""Tests for roster management (9 scenarios)."""

from .common import (XMPPConn, check, reset_counters, summary,
                     create_user, delete_user, sasl_plain, DOMAIN)


def _login(username, password, resource='test'):
    """Connect, authenticate, bind, return XMPPConn (session active)."""
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

    delete_user('rosuser1')
    delete_user('rosuser2')
    create_user('rosuser1', 'rospass1')
    create_user('rosuser2', 'rospass2')

    # ── 1. Get empty roster ───────────────────────────────────────────────────
    print('\n[roster-1] Get empty roster')
    c1 = _login('rosuser1', 'rospass1')
    c1.send(
        "<iq type='get' id='r1'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('roster query in response', 'jabber:iq:roster' in resp, resp)
    # No <item> elements in empty roster
    check('no items in empty roster', '<item' not in resp, resp)

    # ── 2. Add rosuser2 to rosuser1's roster ──────────────────────────────────
    print('\n[roster-2] Add item rosuser2@localhost')
    c1.send(
        "<iq type='set' id='r2'>"
        "<query xmlns='jabber:iq:roster'>"
        f"<item jid='rosuser2@{DOMAIN}'/>"
        "</query></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    # Roster push should follow in same recv (both queued in one write cycle)
    check('roster push (set IQ)',
          'jabber:iq:roster' in resp and
          ("type='set'" in resp or 'type="set"' in resp),
          resp)

    # ── 3. Get roster — item present with subscription='none' ─────────────────
    print('\n[roster-3] Get roster after add — item present')
    c1.send(
        "<iq type='get' id='r3'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check(f'rosuser2@{DOMAIN} in roster', f'rosuser2@{DOMAIN}' in resp, resp)
    check('subscription=none', "subscription='none'" in resp or 'subscription="none"' in resp, resp)

    # ── 4. Update item name ────────────────────────────────────────────────────
    print('\n[roster-4] Update item name')
    c1.send(
        "<iq type='set' id='r4'>"
        "<query xmlns='jabber:iq:roster'>"
        f"<item jid='rosuser2@{DOMAIN}' name='Bobby'/>"
        "</query></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('name Bobby in roster push',
          'Bobby' in resp, resp)

    # ── 5. Remove item ────────────────────────────────────────────────────────
    print('\n[roster-5] Remove item (subscription=remove)')
    c1.send(
        "<iq type='set' id='r5'>"
        "<query xmlns='jabber:iq:roster'>"
        f"<item jid='rosuser2@{DOMAIN}' subscription='remove'/>"
        "</query></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('roster push subscription=remove',
          "subscription='remove'" in resp or 'subscription="remove"' in resp, resp)

    # ── 6. Get roster after remove — empty ────────────────────────────────────
    print('\n[roster-6] Get roster after remove — empty')
    c1.send(
        "<iq type='get' id='r6'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c1.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('no items after remove', '<item' not in resp, resp)

    # ── 7. Roster set without jid → bad-request ───────────────────────────────
    print('\n[roster-7] Roster set without jid → bad-request')
    c1.send(
        "<iq type='set' id='r7'>"
        "<query xmlns='jabber:iq:roster'>"
        "<item name='NoBadJid'/>"
        "</query></iq>"
    )
    resp = c1.recv()
    check('bad-request error', 'bad-request' in resp, resp)

    c1.close()

    # ── 8. Roster get before binding → not-allowed ────────────────────────────
    print('\n[roster-8] Roster get before binding → not-allowed')
    c2 = XMPPConn()
    c2.open_stream()
    c2.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('rosuser1', 'rospass1')}</auth>"
    )
    c2.recv()
    c2.open_stream()
    # Send roster get WITHOUT binding first
    c2.send(
        "<iq type='get' id='r8'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c2.recv()
    check('not-allowed error', 'not-allowed' in resp, resp)
    c2.close()

    # ── 9. Roster persistence: add, disconnect, reconnect, verify ─────────────
    print('\n[roster-9] Roster persistence across reconnect')
    c3 = _login('rosuser1', 'rospass1', resource='persist1')
    c3.send(
        "<iq type='set' id='rp1'>"
        "<query xmlns='jabber:iq:roster'>"
        f"<item jid='rosuser2@{DOMAIN}' name='Persistent'/>"
        "</query></iq>"
    )
    c3.recv()
    c3.close()

    # Reconnect
    c4 = _login('rosuser1', 'rospass1', resource='persist2')
    c4.send(
        "<iq type='get' id='rp2'>"
        "<query xmlns='jabber:iq:roster'/></iq>"
    )
    resp = c4.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check(f'rosuser2@{DOMAIN} persisted', f'rosuser2@{DOMAIN}' in resp, resp)
    check('name Persistent persisted', 'Persistent' in resp, resp)
    c4.close()

    # Teardown
    delete_user('rosuser1')
    delete_user('rosuser2')

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
