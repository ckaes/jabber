#!/usr/bin/env python3
"""Tests for message routing: online delivery, offline storage, errors (6 scenarios)."""

import os
import re
import time
from .common import (XMPPConn, check, reset_counters, summary, REPO,
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

    delete_user('msguser1')
    delete_user('msguser2')
    create_user('msguser1', 'msgpass1')
    create_user('msguser2', 'msgpass2')

    # ── 1. Online delivery: msguser1 → msguser2 (both online) ────────────────
    print('\n[msg-1] Online delivery: both users online')
    c1 = _login('msguser1', 'msgpass1', resource='r1')
    c2 = _login('msguser2', 'msgpass2', resource='r2')

    c1.send(
        f"<message to='msguser2@{DOMAIN}' id='m1'>"
        "<body>Hello online</body></message>"
    )
    resp2 = c2.recv(timeout=1.5)
    check('msguser2 receives message', 'Hello online' in resp2, resp2)
    check('from is msguser1 full JID',
          f'msguser1@{DOMAIN}/r1' in resp2, resp2)

    # ── 2. Message to non-existent user → item-not-found error ───────────────
    print('\n[msg-2] Message to non-existent user → item-not-found')
    c1.send(
        f"<message to='nosuchuser@{DOMAIN}' id='m2'>"
        "<body>Ghost</body></message>"
    )
    resp1 = c1.recv(timeout=1.5)
    check('error returned to sender', 'type="error"' in resp1 or "type='error'" in resp1, resp1)
    check('item-not-found condition', 'item-not-found' in resp1, resp1)

    # ── 3. Message to wrong domain → item-not-found error ────────────────────
    print('\n[msg-3] Message to wrong domain → item-not-found')
    c1.send(
        "<message to='someone@otherdomain.example' id='m3'>"
        "<body>Cross domain</body></message>"
    )
    resp1 = c1.recv(timeout=1.5)
    check('error returned to sender', 'type="error"' in resp1 or "type='error'" in resp1, resp1)
    check('item-not-found condition', 'item-not-found' in resp1, resp1)

    # ── 4. Offline storage: msguser2 goes offline, msguser1 sends message ─────
    print('\n[msg-4] Offline storage: msguser2 offline → message stored')
    c2.close()
    time.sleep(0.2)  # let server process disconnect

    c1.send(
        f"<message to='msguser2@{DOMAIN}' id='m4'>"
        "<body>Stored offline</body></message>"
    )
    time.sleep(0.3)

    offline_dir = os.path.join(REPO, 'data', 'msguser2', 'offline')
    xml_files = [f for f in os.listdir(offline_dir) if f.endswith('.xml')] \
                if os.path.isdir(offline_dir) else []
    check('offline file created', len(xml_files) > 0,
          f'offline dir={offline_dir}, files={xml_files}')

    # ── 5. Offline delivery on login: msguser2 reconnects, receives delayed msg
    print('\n[msg-5] Offline delivery on login with delay stamp')
    c2 = _login('msguser2', 'msgpass2', resource='r2b')
    c2.send('<presence/>')
    resp2 = c2.recv(timeout=2.0)
    check('offline message delivered', 'Stored offline' in resp2, resp2)
    check('delay element present', 'urn:xmpp:delay' in resp2, resp2)
    # Verify timestamp format YYYY-MM-DDTHH:MM:SSZ
    stamp_match = re.search(r'stamp=["\'](\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)["\']', resp2)
    check('delay stamp is valid UTC format', stamp_match is not None, resp2)

    # ── 6. <message type='error'> to offline user → NOT stored ───────────────
    print('\n[msg-6] Error message to offline user → not stored in offline dir')
    # Make msguser1 offline for this test (close and reconnect without presence)
    c1.close()
    time.sleep(0.1)
    c1 = _login('msguser1', 'msgpass1', resource='r1b')

    # Clear msguser1's offline dir if present
    m1_offline = os.path.join(REPO, 'data', 'msguser1', 'offline')
    if os.path.isdir(m1_offline):
        for f in os.listdir(m1_offline):
            os.unlink(os.path.join(m1_offline, f))

    c2.send(
        f"<message to='msguser1@{DOMAIN}' type='error' id='m6'>"
        "<body>Error msg</body></message>"
    )
    time.sleep(0.3)

    stored = []
    if os.path.isdir(m1_offline):
        stored = [f for f in os.listdir(m1_offline) if f.endswith('.xml')]
    check('error message not stored offline', len(stored) == 0,
          f'found files: {stored}')

    c1.close()
    c2.close()

    # Teardown
    delete_user('msguser1')
    delete_user('msguser2')

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
