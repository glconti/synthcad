"""Exercise the single-executable workflow in separate caller invocations.

Run phases setup, open, review, export, cleanup with the same --work-dir.
Optionally run restricted from a restricted caller after open.
No session-directory override is used: this also exercises the user's ordinary
registry from the current caller context. All model edits are in an owned copy.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--phase', choices=['setup', 'open', 'restricted', 'review', 'export', 'cleanup'], required=True)
    parser.add_argument('--source-project', type=Path, help='Copy only design.js and synthcad.json from an existing project')
    parser.add_argument('--registry', type=Path, help='Explicit registry for controlled cross-context testing; keep it identical between calls')
    args = parser.parse_args()
    work = args.work_dir.resolve()
    cli = args.cli.resolve(strict=True)
    project = work / 'Minimal project città'
    evidence_path = work / 'workflow-evidence.json'
    env = os.environ.copy()
    env.pop('SYNTHCAD_VIEWER', None)
    env.pop('SYNTHCAD_SESSION_DIR', None)
    if args.registry:
        env['SYNTHCAD_SESSION_DIR'] = str(args.registry.resolve())

    def call(*command, expected=0):
        result = subprocess.run([str(cli), *map(str, command), '--json'], cwd=work,
                                env=env, capture_output=True, timeout=40)
        output = result.stdout.decode('utf-8')
        if result.returncode != expected:
            raise RuntimeError(f'{command}: {result.returncode}: {output} {result.stderr.decode("utf-8", errors="replace")}')
        value = json.loads(output)
        if expected == 0 and not value['ok']:
            raise RuntimeError(value)
        return value

    def save():
        evidence_path.write_text(json.dumps(evidence, indent=2), encoding='utf-8')

    if args.phase == 'setup':
        if work.exists():
            raise RuntimeError('Use a fresh work directory; existing evidence is preserved')
        work.mkdir(parents=True)
        project.mkdir()
        session = 'standalone-qa-' + str(time.time_ns())
        if args.source_project:
            for name in ('design.js', 'synthcad.json'):
                shutil.copy2(args.source_project / name, project / name)
        else:
            guide = call('project', '--help')['data']['guidance']
            source = re.search(r'```javascript\n(.*?)\n```', guide, re.S).group(1)
            manifest = json.loads(re.search(r'```json\n(.*?)\n```', guide, re.S).group(1))
            manifest.update(activeProfile='qa-volume', profiles={'qa-volume': {
                'buildVolume': [120, 120, 120], 'exclusions': []}})
            (project / 'design.js').write_text(source, encoding='utf-8')
            (project / 'synthcad.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        evidence = {'project': session, 'projectPath': str(project), 'cli': str(cli)}
        save()
        print('PASS setup: two authored files only')
        return

    evidence = json.loads(evidence_path.read_text(encoding='utf-8'))
    session = evidence['project']
    if str(cli) != evidence['cli']:
        raise RuntimeError('Executable changed between phases')
    if args.phase == 'open':
        opened = call('project', 'open', project, '--name', session)
        evidence['pid'] = opened['data']['pid']
        save()  # retain ownership evidence even if a later assertion fails
        assert not opened['data']['reused'], opened
        print(f'PASS open: PID {evidence["pid"]}; caller will now exit')
    elif args.phase == 'restricted':
        # When the host sandbox denies pipe access, the CLI must retain the
        # recorded viewer and report io_error, never mislabel it as no_session.
        sessions = call('project', 'list')['data']['projects']
        match = next(r for r in sessions if r['project'] == session)
        assert match['pid'] == evidence['pid']
        if match['reachable']:
            assert call('project', 'open', project, '--name', session)['data']['pid'] == evidence['pid']
            evidence['restrictedAccess'] = 'available'
        else:
            state = call('project', 'inspect', '--project', session, expected=10)
            opened = call('project', 'open', project, '--name', session, expected=10)
            assert state['error']['code'] == opened['error']['code'] == 'io_error'
            if 'details' in opened['error']:
                assert opened['error']['details']['pid'] == evidence['pid']
            else:
                assert 'Windows error 5' in opened['error']['message'], opened
            after = call('project', 'list')['data']['projects']
            assert [r['pid'] for r in after if r['project'] == session] == [evidence['pid']]
            evidence['restrictedAccess'] = {'state': state['error'], 'open': opened['error']}
        save()
        print('PASS restricted call: recorded PID retained; permission failure cannot create a second viewer')
    elif args.phase in ('review', 'export'):
        # This process starts after the previous caller has already exited.
        state = call('project', 'inspect', '--project', session)
        reused = call(project / 'synthcad.json', '--name', session)
        assert reused['data']['reused'] and reused['data']['pid'] == evidence['pid'], reused
        sessions = call('project', 'list')['data']['projects']
        matches = [r for r in sessions if r['project'] == session]
        assert len(matches) == 1 and matches[0]['pid'] == evidence['pid'], matches
        if os.name == 'nt':
            user = ctypes.windll.user32
            windows = []
            callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
            def visit(window, unused):
                pid = ctypes.c_ulong()
                user.GetWindowThreadProcessId(ctypes.c_void_p(window), ctypes.byref(pid))
                if pid.value == evidence['pid'] and user.IsWindowVisible(ctypes.c_void_p(window)):
                    windows.append(int(window))
                return True
            user.EnumWindows(callback_type(visit), 0)
            assert len(windows) == 1, f'Expected one visible viewer, got {windows}'
            evidence['visibleViewerWindows'] = len(windows)
        deadline = time.monotonic() + 30
        while state['data']['status'] == 'loading' and time.monotonic() < deadline:
            time.sleep(0.1)
            state = call('project', 'inspect', '--project', session)
        assert state['data']['status'] == 'ready', state
        if args.phase == 'review':
            evidence['displayedBeforeEdit'] = state['data']['displayedRevision']
            source = project / 'design.js'
            text = source.read_text(encoding='utf-8')
            if 'const width = 80;' in text:
                text = text.replace('const width = 80;', 'const width = 81;')
            else:
                text += '\n// QA copy: revision acknowledgement edit.\n'
            source.write_text(text, encoding='utf-8')
            desired = call('project', 'revision', '--project', session)['data']['revision']
            ready = call('project', 'wait', '--project', session, '--revision', desired, '--timeout', '30000')
            assert ready['data']['displayedRevision'] != evidence['displayedBeforeEdit'], ready
            evidence['displayedAfterEdit'] = ready['data']['displayedRevision']
            print(f'PASS review: same PID {evidence["pid"]}, one viewer, positional reuse and hot reload')
        else:
            manifest = json.loads((project / 'synthcad.json').read_text(encoding='utf-8'))
            plate = next(name for name in manifest['views'] if 'plate' in name)
            call('review', 'view', plate, '--project', session)
            desired = call('project', 'revision', '--project', session)['data']['revision']
            ready = call('project', 'wait', '--project', session, '--revision', desired, '--timeout', '30000')
            output = project / 'exports' / 'model.3mf'
            output.parent.mkdir(exist_ok=True)
            call('print', 'export', output, '--project', session, '--expect-revision', ready['revision'], '--allow-warnings')
            assert output.exists()
            assert {p.name for p in project.iterdir()} == {'design.js', 'synthcad.json', 'exports', '.synthcad'}
            assert {p.name for p in output.parent.iterdir()} == {'model.3mf'}
            evidence['export'] = str(output)
            print('PASS export: same viewer; only two sources, requested 3MF and app-managed history')
        save()
    else:
        # Only terminate the PID whose live session and project still match.
        sessions = call('project', 'list')['data']['projects']
        match = next((r for r in sessions if r['project'] == session), None)
        if match:
            assert match['pid'] == evidence['pid']
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(evidence['pid']), '/F'], check=True, capture_output=True)
            else:
                os.kill(evidence['pid'], 15)
        print('PASS cleanup: stopped only the test-owned viewer; evidence retained')


if __name__ == '__main__':
    main()
