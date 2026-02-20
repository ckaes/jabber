#!/usr/bin/env python3
"""
Test orchestrator: starts xmppd, runs all test modules, reports results.

The server is (re)started before each module. This ensures a fresh server
even if a previous module triggered a server crash (e.g. via a known
use-after-free in stream error handling from within SAX startElement callbacks).

Usage:
    python3 tests/run_all.py          # run everything
    python3 -m tests.run_all          # alternate invocation
"""

import os
import sys
import time
import subprocess
import socket

# Add repo root to path so `import tests.*` works when run from repo root
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)

from tests import (test_auth, test_session, test_roster,
                   test_presence, test_message, test_disco,
                   test_registration)

XMPPD    = os.path.join(REPO, 'xmppd')
CONF     = os.path.join(REPO, 'config', 'xmppd.conf.example')
PORT     = 5222


def _wait_for_port(host='127.0.0.1', port=PORT, timeout=3.0):
    """Block until TCP port is accepting connections (or timeout expires)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=0.2)
            s.close()
            return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.1)
    return False


def _kill_existing():
    """Kill any xmppd process already listening on PORT."""
    try:
        subprocess.run(['pkill', '-f', r'\bxmppd\b'], capture_output=True)
        time.sleep(0.3)
    except Exception:
        pass


def start_server():
    """Start xmppd in the background. Returns the Popen object."""
    _kill_existing()
    proc = subprocess.Popen(
        [XMPPD, '-c', CONF, '-L', 'WARN'],
        cwd=REPO,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if not _wait_for_port(timeout=3.0):
        proc.terminate()
        sys.exit('ERROR: xmppd did not start within 3 seconds')
    return proc


def stop_server(proc):
    """Terminate the server process (best-effort)."""
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def ensure_server(proc):
    """
    If the server process is still alive, return it unchanged.
    If it has died (crash), start a fresh one and return the new Popen.
    """
    if proc is None or proc.poll() is not None:
        print('  [orchestrator] server died — restarting...')
        return start_server()
    return proc


def run_module(name, mod, proc):
    """
    Run a test module.
    Returns (pass_count, fail_count, server_proc).
    Restarts the server before the module if needed.
    """
    proc = ensure_server(proc)
    print(f'\n{"═"*50}')
    print(f'Module: {name}')
    print('═'*50)
    try:
        mod.run()
        from tests import common
        return common.PASS_COUNT, common.FAIL_COUNT, proc
    except Exception as exc:
        print(f'  ERROR: module {name} raised exception: {exc}')
        import traceback
        traceback.print_exc()
        return 0, 1, proc


def main():
    proc = start_server()
    print(f'xmppd started (pid {proc.pid})')

    modules = [
        ('registration', test_registration),
        ('auth',         test_auth),
        ('session',      test_session),
        ('roster',       test_roster),
        ('presence',     test_presence),
        ('message',      test_message),
        ('disco',        test_disco),
    ]

    total_pass = 0
    total_fail = 0

    for name, mod in modules:
        passed, failed, proc = run_module(name, mod, proc)
        total_pass += passed
        total_fail += failed

    stop_server(proc)
    print(f'\n{"═"*50}')
    print(f'GRAND TOTAL: {total_pass} passed, {total_fail} failed')
    print('═'*50)
    sys.exit(0 if total_fail == 0 else 1)


if __name__ == '__main__':
    main()
