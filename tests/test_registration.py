#!/usr/bin/env python3
"""Tests for XEP-0077 in-band registration (17 scenarios)."""

import os
import shutil
from .common import (XMPPConn, check, reset_counters, summary,
                     sasl_plain, REPO, DOMAIN)

NEWUSER_DIR = os.path.join(REPO, 'data', 'newuser')


def run():
    reset_counters()

    # Clean up any leftover newuser from a previous run
    if os.path.exists(NEWUSER_DIR):
        shutil.rmtree(NEWUSER_DIR)

    # ── 1. Pre-auth stream features ───────────────────────────────────────────
    print('\n[reg-1] Pre-auth stream features')
    c = XMPPConn()
    resp = c.open_stream()
    check('register feature advertised',
          'http://jabber.org/features/iq-register' in resp, resp)
    c.close()

    # ── 2. Registration form (IQ get) ─────────────────────────────────────────
    print('\n[reg-2] Registration form (IQ get)')
    c = XMPPConn()
    c.open_stream()
    c.send("<iq type='get' id='reg1'><query xmlns='jabber:iq:register'/></iq>")
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    check('<username/> field present', '<username' in resp, resp)
    check('<password/> field present', '<password' in resp, resp)
    c.close()

    # ── 3. Create new account ─────────────────────────────────────────────────
    print('\n[reg-3] Create new account (newuser / testpass)')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<iq type='set' id='reg2'>"
        "<query xmlns='jabber:iq:register'>"
        "<username>newuser</username>"
        "<password>testpass</password>"
        "</query></iq>"
    )
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    c.close()

    # ── 4. Verify data directory ──────────────────────────────────────────────
    print('\n[reg-4] data/newuser/ created on disk')
    check('data/newuser/ exists',   os.path.isdir(NEWUSER_DIR))
    check('user.conf exists',       os.path.isfile(f'{NEWUSER_DIR}/user.conf'))
    check('roster.xml exists',      os.path.isfile(f'{NEWUSER_DIR}/roster.xml'))
    check('offline/ exists',        os.path.isdir(f'{NEWUSER_DIR}/offline'))

    # ── 5. Authenticate as newuser ────────────────────────────────────────────
    print('\n[reg-5] Authenticate as newuser / testpass')
    c = XMPPConn()
    c.open_stream()
    c.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('newuser', 'testpass')}</auth>"
    )
    resp = c.recv()
    check('<success/> received', '<success' in resp, resp)
    c.open_stream()
    c.send(
        "<iq type='set' id='b1'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        "<resource>test</resource></bind></iq>"
    )
    bind_resp = c.recv()
    check('resource bound', 'bind' in bind_resp, bind_resp)

    # ── 6. Duplicate registration → conflict ──────────────────────────────────
    print('\n[reg-6] Duplicate registration → conflict')
    c2 = XMPPConn()
    c2.open_stream()
    c2.send(
        "<iq type='set' id='reg3'>"
        "<query xmlns='jabber:iq:register'>"
        "<username>newuser</username>"
        "<password>other</password>"
        "</query></iq>"
    )
    resp2 = c2.recv()
    check('<conflict/> error returned', 'conflict' in resp2, resp2)
    c2.close()

    # ── 7. Post-auth password change ──────────────────────────────────────────
    print('\n[reg-7] Post-auth password change (→ newpass)')
    c.send(
        "<iq type='set' id='pw1'>"
        "<query xmlns='jabber:iq:register'>"
        "<username>newuser</username>"
        "<password>newpass</password>"
        "</query></iq>"
    )
    resp = c.recv()
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    conf_path = f'{NEWUSER_DIR}/user.conf'
    conf = open(conf_path).read() if os.path.exists(conf_path) else ''
    check('user.conf contains newpass', 'newpass' in conf, conf)

    # ── 8. Account removal ────────────────────────────────────────────────────
    print('\n[reg-8] Post-auth account removal')
    c.send(
        "<iq type='set' id='rm1'>"
        "<query xmlns='jabber:iq:register'><remove/></query></iq>"
    )
    resp = c.recv(timeout=1.5)
    check('result IQ returned',
          'type="result"' in resp or "type='result'" in resp, resp)
    import time
    time.sleep(0.5)
    check('data/newuser/ removed', not os.path.exists(NEWUSER_DIR))
    c.close()

    # ── 9. disco#info includes jabber:iq:register ──────────────────────────────
    print('\n[reg-9] disco#info includes jabber:iq:register')
    c3 = XMPPConn()
    c3.open_stream()
    c3.send(
        "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
        f"{sasl_plain('alice', 'secret')}</auth>"
    )
    c3.recv()
    c3.open_stream()
    c3.send(
        "<iq type='set' id='b2'>"
        "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
        "<resource>test</resource></bind></iq>"
    )
    c3.recv()
    c3.send(
        f"<iq type='get' id='d1' to='{DOMAIN}'>"
        "<query xmlns='http://jabber.org/protocol/disco#info'/></iq>"
    )
    resp = c3.recv()
    check('jabber:iq:register in disco#info', 'jabber:iq:register' in resp, resp)
    c3.close()

    return summary()


if __name__ == '__main__':
    import sys
    sys.exit(0 if run() else 1)
