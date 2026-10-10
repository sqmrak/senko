#!/usr/bin/env python3
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'packaging/var/jb/usr/lib/senko/launchd.sh'
POSTINST = ROOT / 'packaging/DEBIAN/postinst'

def shell_function(path, name):
    # extraction excludes the installer's filesystem and process mutations
    match = re.search(r'^' + name + r'\(\) \{\n.*?^\}\n',
                      path.read_text(), re.M | re.S)
    if not match:
        raise AssertionError('missing function ' + name)
    return match.group(0)


FAKE_LAUNCHCTL = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

base = Path(os.environ['LAUNCHCTL_FIXTURE'])
cfg = json.loads((base / 'config').read_text())
args = sys.argv[1:]
context = os.environ.get('FIXTURE_CONTEXT', 'direct')
with (base / 'calls').open('a') as out:
    out.write(json.dumps([context, args, os.environ.get('LAUNCHD_SOCKET'),
                          os.environ.get('LaunchKeepContext')]) + '\n')
if 'LAUNCHD_SOCKET' in os.environ:
    print('launch_msg(): Socket is not connected', file=sys.stderr)
    sys.exit(1)
if args[0] == '-1':
    if cfg.get('modern'):
        sys.exit(1)
    context = 'system'
    args = args[1:]
if args[0] == 'bsexec':
    if not cfg.get('bsexec'):
        sys.exit(1)
    env = dict(os.environ, FIXTURE_CONTEXT='bsexec', LaunchKeepContext='1')
    os.execve(args[2], args[2:], env)
system = context == cfg.get('system_context', 'direct')
cmd = args[0]
if cmd == 'print':
    if cfg.get('modern') and system:
        print('system = {\n}')
        sys.exit(0)
    # some jailbreak launchctl builds return zero for unsupported commands
    sys.exit(0 if cfg.get('noop') else 1)
if cmd == 'managername':
    if cfg.get('modern'):
        sys.exit(1)
    print('System' if system else 'Aqua')
    sys.exit(0)
if not system:
    # a mobile manager can list jobs even though LaunchDaemons belong elsewhere
    if cmd == 'list':
        print('PID\tStatus\tLabel\n12\t0\tcom.apple.mobile.fixture')
        sys.exit(0)
    print('launch_msg(): Socket is not connected', file=sys.stderr)
    sys.exit(1)
job = base / 'loaded'
if cmd == 'list':
    if cfg.get('list_failure') or (cfg.get('list_failure_loaded') and job.exists()):
        print('launch_msg(): Socket is not connected', file=sys.stderr)
        sys.exit(1)
    print('PID\tStatus\tLabel')
    print('42\t0\tcom.senko.senkod.other')
    if job.exists():
        print('43\t0\tcom.senko.senkod')
    sys.exit(0)
if cmd in ('load', 'bootstrap'):
    expected = 'bootstrap' if cfg.get('modern') else 'load'
    if cfg.get('reject_load'):
        print('fixture: invalid plist permissions', file=sys.stderr)
        sys.exit(1)
    if cmd == expected:
        job.touch()
        sys.exit(0)
    sys.exit(0 if cfg.get('noop') else 1)
if cmd in ('bootout', 'unload', 'remove'):
    expected = cfg.get('stop_command', 'bootout' if cfg.get('modern') else 'unload')
    if cmd == expected and not cfg.get('reject_stop'):
        job.unlink(missing_ok=True)
        sys.exit(0)
    sys.exit(0 if cfg.get('noop') else 1)
if cmd in ('enable', 'kickstart', 'start'):
    sys.exit(0 if cfg.get('modern') or cmd == 'start' else 1)
sys.exit(1)
'''


class LaunchdInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='senko-launchd-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.launchctl = self.base / 'launchctl'
        self.launchctl.write_text(FAKE_LAUNCHCTL)
        self.launchctl.chmod(0o755)
        self.killall = self.base / 'killall'
        self.killall.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_KILLS"\n')
        self.killall.chmod(0o755)

    def run_shell(self, command, config=None, loaded=False, answers=True):
        (self.base / 'config').write_text(json.dumps(config or {}))
        if loaded:
            (self.base / 'loaded').touch()
        (self.base / 'calls').write_text('')
        functions = ''.join(shell_function(POSTINST, name) for name in (
            'postinst_log_lines', 'launchd_attempt', 'diagnose_launchd_load',
            'launchd_load_and_kick', 'launchd_start'))
        functions += shell_function(ROOT / 'packaging/DEBIAN/prerm', 'stop_daemon')
        script = '. "$HELPER"\n' + functions + '''
postinst_log() { printf '%s\\n' "$*" >> "$TEST_LOG"; }
diagnose_senkod() { postinst_log "diagnose_senkod"; }
senkod_answers() { return "$TEST_ANSWERS"; }
sleep() { :; }
''' + command
        env = dict(os.environ, HELPER=str(HELPER), LAUNCHCTL=str(self.launchctl),
                   PLIST='/fixture/Library/LaunchDaemons/com.senko.senkod.plist',
                   LAUNCHCTL_FIXTURE=str(self.base), TEST_LOG=str(self.base / 'log'),
                   TEST_ANSWERS='0' if answers else '1',
                   KILLALL=str(self.killall), TEST_KILLS=str(self.base / 'kills'),
                   LAUNCHD_SOCKET='/stale/mobile/socket', LaunchKeepContext='1')
        self.result = subprocess.run(['/bin/sh', '-c', script], env=env,
                                     text=True, capture_output=True, timeout=10)
        self.calls = [json.loads(line) for line in (self.base / 'calls').read_text().splitlines()]
        self.log = (self.base / 'log').read_text() if (self.base / 'log').exists() else ''
        return self.result

    def test_legacy_installer_selects_system(self):
        result = self.run_shell('launchd_start; rc=$?; echo "$LAUNCHCTL_CONTEXT"; exit "$rc"',
                                {'system_context': 'system'})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'system')
        self.assertTrue((self.base / 'loaded').exists())
        self.assertIn('senkod answered STATUS', self.log)
        self.assertTrue(all(call[2] is None and call[3] is None for call in self.calls))

    def test_legacy_bsexec_fallback(self):
        result = self.run_shell('launchd_start; rc=$?; echo "$LAUNCHCTL_CONTEXT"; exit "$rc"',
                                {'system_context': 'bsexec', 'bsexec': True})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'bsexec')
        self.assertTrue((self.base / 'loaded').exists())
        self.assertTrue(all(call[2] is None for call in self.calls))

    def test_modern_uses_system_domain(self):
        result = self.run_shell('launchd_start', {'modern': True})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(any(call[1][0] in ('-1', 'bsexec', 'load') for call in self.calls))

    def test_h3lix_noop_bootstrap_falls_back_to_load(self):
        result = self.run_shell('launchd_start', {'noop': True})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any(call[1][:2] == ['load', '-w'] for call in self.calls))

    def test_unreachable_system_is_failure(self):
        result = self.run_shell('launchd_start', {'system_context': 'unreachable', 'noop': True})
        self.assertEqual(result.returncode, 1)
        self.assertIn('cannot reach system launchd', self.log)
        self.assertFalse(any(call[1][0] in ('bootstrap', 'load') for call in self.calls))

    def test_rejected_plist_keeps_reason(self):
        result = self.run_shell('launchd_start', {'reject_load': True})
        self.assertEqual(result.returncode, 1)
        self.assertIn('fixture: invalid plist permissions', result.stderr)
        self.assertIn('fixture: invalid plist permissions', self.log)
        self.assertIn('diag session:', self.log)

    def test_similar_label_is_not_loaded(self):
        result = self.run_shell('select_launchctl_context && launchd_loaded')
        self.assertEqual(result.returncode, 1)

    def test_failed_list_is_not_absence(self):
        result = self.run_shell('launchd_loaded', {'list_failure': True})
        self.assertEqual(result.returncode, 2)

    def test_loaded_but_unresponsive_is_reported(self):
        result = self.run_shell('launchd_start', answers=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn('senkod did not answer STATUS', self.log)
        self.assertIn('diagnose_senkod', self.log)

    def test_stop_legacy_uses_same_context(self):
        result = self.run_shell('launchd_stop', {'system_context': 'system', 'noop': True}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.base / 'loaded').exists())
        self.assertTrue(any(call[1][0:2] == ['-1', 'unload'] for call in self.calls))

    def test_stop_modern(self):
        result = self.run_shell('launchd_stop', {'modern': True}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.base / 'loaded').exists())
        self.assertFalse(any(call[1][0] == 'unload' for call in self.calls))

    def test_stop_remove_fallback(self):
        result = self.run_shell('launchd_stop', {'noop': True, 'stop_command': 'remove'}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.base / 'loaded').exists())

    def test_stop_failure_does_not_claim_unloaded(self):
        result = self.run_shell('launchd_stop', {'noop': True, 'reject_stop': True}, loaded=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('still holds', result.stderr)
        self.assertTrue((self.base / 'loaded').exists())

    def test_stop_list_failure(self):
        result = self.run_shell('launchd_stop', {'list_failure_loaded': True}, loaded=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_shell('launchd_stop', {'list_failure_loaded': True}, loaded=True)
        self.assertEqual(result.returncode, 1)
        self.assertTrue((self.base / 'loaded').exists())

    def test_prerm_stops_processes_after_unload(self):
        result = self.run_shell('stop_daemon', {'system_context': 'system'}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.base / 'loaded').exists())
        self.assertEqual((self.base / 'kills').read_text().splitlines(),
                         ['-TERM senkod', '-TERM senkod', '-9 senkod', '-9 senko-core SenkoTunnel'])

    def test_prerm_does_not_kill_a_loaded_job(self):
        result = self.run_shell('stop_daemon', {'reject_stop': True}, loaded=True)
        self.assertEqual(result.returncode, 1)
        self.assertTrue((self.base / 'loaded').exists())
        self.assertFalse((self.base / 'kills').exists())

    def test_prerm_does_not_kill_with_unknown_context(self):
        result = self.run_shell('stop_daemon', {'system_context': 'unreachable'}, loaded=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse((self.base / 'kills').exists())


if __name__ == '__main__':
    unittest.main()
