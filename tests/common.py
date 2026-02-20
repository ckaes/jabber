#!/usr/bin/env python3
"""Shared helpers for XMPP integration tests."""

import socket
import base64
import os
import shutil
import subprocess

REPO   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOMAIN = 'localhost'
PORT   = 5222

PASS_COUNT = 0
FAIL_COUNT = 0


def sasl_plain(username, password):
    """Encode SASL PLAIN token: base64(\0username\0password)."""
    token = f'\x00{username}\x00{password}'.encode()
    return base64.b64encode(token).decode()


def check(label, condition, detail=''):
    """Record a pass/fail result and print it."""
    global PASS_COUNT, FAIL_COUNT
    if condition:
        print(f'  PASS  {label}')
        PASS_COUNT += 1
    else:
        print(f'  FAIL  {label}')
        if detail:
            print(f'        got: {detail!r}')
        FAIL_COUNT += 1


def reset_counters():
    """Reset global pass/fail counters (called by each module before run())."""
    global PASS_COUNT, FAIL_COUNT
    PASS_COUNT = 0
    FAIL_COUNT = 0


def summary():
    """Print totals and return True if all tests passed."""
    total = PASS_COUNT + FAIL_COUNT
    print(f'\n{"─"*40}')
    print(f'Passed: {PASS_COUNT}/{total}')
    if FAIL_COUNT:
        print(f'Failed: {FAIL_COUNT}/{total}')
    return FAIL_COUNT == 0


def create_user(username, password):
    """Create a user via the useradd tool."""
    useradd = os.path.join(REPO, 'useradd')
    datadir = os.path.join(REPO, 'data')
    result = subprocess.run(
        [useradd, '-d', datadir, '-u', username, '-p', password],
        capture_output=True, text=True
    )
    return result.returncode == 0


def delete_user(username):
    """Remove a user's data directory."""
    user_dir = os.path.join(REPO, 'data', username)
    if os.path.exists(user_dir):
        shutil.rmtree(user_dir)


class XMPPConn:
    """A raw TCP connection to the XMPP server."""

    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect(('127.0.0.1', PORT))
        self.s.settimeout(2.0)

    def send(self, data):
        self.s.sendall(data.encode() if isinstance(data, str) else data)

    def recv(self, timeout=2.0):
        self.s.settimeout(timeout)
        parts = []
        try:
            while True:
                chunk = self.s.recv(4096)
                if not chunk:
                    break
                parts.append(chunk.decode(errors='replace'))
        except (socket.timeout, ConnectionResetError):
            pass
        return ''.join(parts)

    def open_stream(self):
        self.send(
            "<?xml version='1.0'?>"
            f"<stream:stream to='{DOMAIN}' "
            "xmlns='jabber:client' "
            "xmlns:stream='http://etherx.jabber.org/streams' "
            "version='1.0'>"
        )
        return self.recv()

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass

    def login(self, username, password, resource='test'):
        """
        Full login sequence: open_stream → SASL auth → open_stream → bind.
        Returns (success: bool, bind_response: str).
        """
        self.open_stream()
        self.send(
            "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>"
            f"{sasl_plain(username, password)}</auth>"
        )
        auth_resp = self.recv()
        if '<success' not in auth_resp:
            return False, auth_resp

        self.open_stream()
        self.send(
            f"<iq type='set' id='bind1'>"
            f"<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
            f"<resource>{resource}</resource></bind></iq>"
        )
        bind_resp = self.recv()
        ok = 'bind' in bind_resp and ('type="result"' in bind_resp or "type='result'" in bind_resp)
        return ok, bind_resp
