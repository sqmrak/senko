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
if cfg.get('socket_lost') and (cmd != 'list' or len(args) > 1):
    # ios 6 launchctl load prints this and still exits 0
    print('launch_msg(): Socket is not connected', file=sys.stderr)
    sys.exit(1 if cmd == 'list' else 0)
if cmd == 'list':
    if cfg.get('list_failure') or (cfg.get('list_failure_loaded') and job.exists()):
        print('launch_msg(): Socket is not connected', file=sys.stderr)
        sys.exit(1)
    print('PID\tStatus\tLabel')
    print('42\t0\tcom.senko.senkod.other')
    if job.exists():
        pid = base / 'pid'
        print((pid.read_text().strip() if pid.exists() else '43') + '\t-\tcom.senko.senkod')
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
            'launchd_load_and_kick', 'launchd_start', 'request_reboot',
            'launchd_restart_in_place'))
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

    # kill stands in for launchd: the TERMed pid comes back as 44 through KeepAlive
    RESPAWN = 'kill() { printf "kill %s\\n" "$*" >> "$TEST_KILLS"; echo 44 > "$LAUNCHCTL_FIXTURE/pid"; }\n'

    def test_lost_socket_is_detected(self):
        result = self.run_shell('select_launchctl_context && launchd_socket_lost',
                                {'socket_lost': True}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_shell('select_launchctl_context && launchd_socket_lost', loaded=True)
        self.assertEqual(result.returncode, 1)

    def test_lost_socket_restarts_through_keepalive(self):
        result = self.run_shell(self.RESPAWN + 'select_launchctl_context && launchd_restart_in_place',
                                {'socket_lost': True}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.base / 'kills').read_text().splitlines(), ['kill -TERM 43'])
        self.assertIn('pid 44, senkod answered STATUS', self.log)
        self.assertFalse(any(call[1][0] in ('load', 'unload', 'remove', 'bootout') for call in self.calls))

    def test_lost_socket_does_not_take_old_pid_as_restart(self):
        result = self.run_shell('kill() { :; }\nselect_launchctl_context && launchd_restart_in_place',
                                {'socket_lost': True}, loaded=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('did not bring back an answering senkod after pid 43', self.log)

    def test_lost_socket_without_job_asks_cydia_to_reboot(self):
        result = self.run_shell('exec 7>"$LAUNCHCTL_FIXTURE/cydia"\n'
                                'CYDIA="7 1"\nselect_launchctl_context && launchd_restart_in_place',
                                {'socket_lost': True})
        self.assertEqual(result.returncode, 3)
        self.assertIn('until the device reboots', result.stderr)
        self.assertEqual((self.base / 'cydia').read_text(), 'finish:reboot\n')

    def test_cydia_descriptor_must_be_numeric(self):
        result = self.run_shell('CYDIA="1;touch $LAUNCHCTL_FIXTURE/ran 2"\nrequest_reboot')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.base / 'ran').exists())

    def test_prerm_upgrade_keeps_job_with_lost_socket(self):
        for action in ('upgrade', 'failed-upgrade'):
            result = self.run_shell('stop_daemon ' + action, {'socket_lost': True}, loaded=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((self.base / 'kills').exists())

    def test_prerm_remove_kills_with_lost_socket(self):
        result = self.run_shell('stop_daemon remove', {'socket_lost': True}, loaded=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('until the device reboots', result.stderr)
        self.assertEqual((self.base / 'kills').read_text().splitlines()[0], '-TERM senkod')


class RootfulHealTests(unittest.TestCase):
    """a payload binary the system kills at exec is run from a copy that starts, or the daemon would never come up"""

    FUNCTIONS = ['binary_starts', 'find_device_ldid', 'stage_rootful_copy', 'heal_rootful_binary']

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='senko-heal-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.payload = self.base / 'jb/usr/bin'
        self.rootful = self.base / 'bin'
        self.payload.mkdir(parents=True)
        self.rootful.mkdir()
        self.log = self.base / 'log'

    def program(self, path, killed_in):
        # a program the fixture "kernel" kills when it runs from a path that contains killed_in
        path.write_text('#!/bin/sh\ncase "$0" in *%s*) kill -9 $$ ;; esac\nexit 2\n' % killed_in)
        path.chmod(0o755)

    def run_heal(self, name='senkod', ldid=None, args=''):
        functions = '\n'.join(shell_function(POSTINST, f) for f in self.FUNCTIONS)
        env_ldid = ''
        if ldid:
            env_ldid = 'LDID_CANDIDATES="%s"\n' % ldid
        else:
            env_ldid = 'LDID_CANDIDATES=""\n'
        script = ('PAYLOAD_ROOT="%s"\nROOTFUL_BIN="%s"\nROOTFUL_COPY_MARK="%s"\nJBROOT=\n%s'
                  'postinst_log() { echo "$*" >> "%s"; }\n%s\nheal_rootful_binary %s %s\n' %
                  (self.base / 'jb', self.rootful, self.base / 'mark', env_ldid, self.log, functions, name, args))
        return subprocess.run(['/bin/sh', '-c', script], capture_output=True, text=True)

    def log_text(self):
        return self.log.read_text() if self.log.exists() else ''

    def test_working_payload_is_left_alone(self):
        self.program(self.payload / 'senkod', 'nothing-matches-this')
        self.assertEqual(self.run_heal().returncode, 0)
        self.assertFalse((self.rootful / 'senkod').exists())
        self.assertFalse((self.base / 'mark').exists())
        self.assertEqual(self.log_text(), '')

    def test_always_copies_a_payload_that_starts(self):
        self.program(self.payload / 'senkod', 'nothing-matches-this')
        (self.rootful / 'senkod').symlink_to(self.payload / 'senkod')
        self.assertEqual(self.run_heal(args='"" "" always').returncode, 0)
        copy = self.rootful / 'senkod'
        self.assertTrue(copy.is_file() and not copy.is_symlink())
        self.assertEqual(subprocess.run([str(copy)]).returncode, 2)
        self.assertTrue((self.base / 'mark').exists())

    def test_killed_payload_is_replaced_by_a_copy_on_the_system_volume(self):
        self.program(self.payload / 'senkod', 'jb/usr/bin')
        (self.rootful / 'senkod').symlink_to(self.payload / 'senkod')
        self.assertEqual(self.run_heal().returncode, 0)
        copy = self.rootful / 'senkod'
        self.assertTrue(copy.is_file() and not copy.is_symlink())
        self.assertEqual(subprocess.run([str(copy)]).returncode, 2)
        self.assertTrue((self.base / 'mark').exists())
        self.assertIn('real copy on the system volume', self.log_text())
        self.assertFalse((self.rootful / '.senkod.senko-new').exists())

    def test_a_copy_that_is_killed_too_is_signed_again(self):
        self.program(self.payload / 'senkod', '')
        ldid = self.base / 'ldid'
        # the fixture ldid makes the staged file a program the kernel accepts
        ldid.write_text('#!/bin/sh\nfor last; do :; done\nprintf "#!/bin/sh\\nexit 2\\n" > "$last"\n')
        ldid.chmod(0o755)
        result = self.run_heal(ldid=str(ldid))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([str(self.rootful / 'senkod')]).returncode, 2)
        self.assertIn('signed again', self.log_text())
        self.assertTrue((self.base / 'mark').exists())

    def test_a_killed_core_is_copied_into_the_directory_it_is_given(self):
        lib = self.base / 'jb/usr/lib'
        lib.mkdir(parents=True)
        self.program(lib / 'senko-core', 'jb/usr/lib')
        syslib = self.base / 'lib'
        syslib.mkdir()
        result = self.run_heal(name='senko-core', args='"%s" "%s"' % (lib / 'senko-core', syslib))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([str(syslib / 'senko-core')]).returncode, 2)
        self.assertFalse((syslib / '.senko-core.senko-new').exists())
        self.assertFalse((self.rootful / 'senko-core').exists())

    def test_the_second_ldid_form_is_tried_when_the_first_does_not_help(self):
        self.program(self.payload / 'senkod', '')
        ldid = self.base / 'ldid'
        # only the form with -Hsha1 produces a program that starts
        ldid.write_text('#!/bin/sh\ncase "$*" in *-Hsha1*) for last; do :; done; '
                        'printf "#!/bin/sh\\nexit 2\\n" > "$last";; esac\nexit 0\n')
        ldid.chmod(0o755)
        self.assertEqual(self.run_heal(ldid=str(ldid)).returncode, 0)
        self.assertEqual(subprocess.run([str(self.rootful / 'senkod')]).returncode, 2)
        self.assertIn('-Hsha1', self.log_text())

    def test_no_remedy_leaves_nothing_behind(self):
        self.program(self.payload / 'senkod', '')
        (self.rootful / 'senkod').symlink_to(self.payload / 'senkod')
        result = self.run_heal()
        self.assertEqual(result.returncode, 1)
        # the link stays, the staged file and the marker do not
        self.assertTrue((self.rootful / 'senkod').is_symlink())
        self.assertFalse((self.rootful / '.senkod.senko-new').exists())
        self.assertFalse((self.base / 'mark').exists())
        self.assertIn('no remedy', self.log_text())

    def test_removal_takes_only_the_copies_this_package_made(self):
        postrm = ROOT / 'packaging/DEBIAN/postrm'
        function = shell_function(postrm, 'remove_rootful_copies')
        (self.rootful / 'senkod').write_text('copy')
        (self.rootful / 'senkoctl').symlink_to(self.payload)
        script = 'ROOTFUL_BIN="%s"\nROOTFUL_COPY_MARK="%s"\n%s\nremove_rootful_copies\n' % (self.rootful, self.base / 'mark', function)
        # without the marker nothing of the system's is touched
        self.assertEqual(subprocess.run(['/bin/sh', '-c', script]).returncode, 0)
        self.assertTrue((self.rootful / 'senkod').exists())
        (self.base / 'mark').write_text('')
        self.assertEqual(subprocess.run(['/bin/sh', '-c', script]).returncode, 0)
        self.assertFalse((self.rootful / 'senkod').exists())
        # a link is the layout's own, and its removal is another function's business
        self.assertTrue((self.rootful / 'senkoctl').is_symlink())
        self.assertFalse((self.base / 'mark').exists())

    def test_a_missing_payload_binary_is_not_an_error(self):
        self.assertEqual(self.run_heal('senkoctl').returncode, 0)
        self.assertEqual(self.log_text(), '')


if __name__ == '__main__':
    unittest.main()
