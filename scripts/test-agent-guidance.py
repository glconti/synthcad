"""Traverse the domain help tree from an isolated directory with no viewer or registry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    source = args.cli.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='synthcad-guidance-città-') as temporary:
        root = Path(temporary)
        executable = root / source.name
        shutil.copy2(source, executable)
        env = dict(os.environ, SYNTHCAD_VIEWER=str(root / 'no-viewer'),
                   SYNTHCAD_SESSION_DIR=str(root / 'no-projects'))

        def call(*arguments, exit_code=0):
            result = subprocess.run([str(executable), *arguments], cwd=root, env=env,
                                    capture_output=True, timeout=15)
            assert result.returncode == exit_code, (arguments, result.returncode, result.stderr, result.stdout)
            assert not result.stderr, result.stderr
            return result.stdout.decode('utf-8').replace('\r\n', '\n')

        assert call() == call('--help')
        kickstart = call()
        for phrase in ('human', 'JavaScript', 'persistent', 'REQUESTED_TOKEN', 'DISPLAYED_TOKEN',
                       'physical', 'design.js', 'synthcad.json', 'exports/', 'temporary'):
            assert phrase in kickstart, phrase
        root_data = json.loads(call('--help', '--json'))['data']
        assert {n['path'] for n in root_data['children']} == {'project', 'model', 'review', 'print'}
        assert root_data['capabilities']['geometryEditing'] is False
        assert root_data['capabilities']['automaticPacking'] is False
        assert root_data['capabilities']['exportFormats'] == ['3mf', 'stl']
        pending, seen, actions = [''], {}, []
        while pending:
            path = pending.pop()
            assert path not in seen, path
            words = path.split()
            envelope = json.loads(call(*words, '--help', '--json'))
            assert envelope['ok'] and envelope['protocolVersion'] == 2 and envelope['command'] == path
            data = envelope['data']
            seen[path] = data
            assert data['path'] == path
            for field in ('kind', 'summary', 'usage', 'children', 'arguments', 'options', 'requirements',
                          'examples', 'guidance', 'nextSteps', 'hash', 'bundleHash', 'bundleVersion'):
                assert field in data, (path, field)
            assert data['guidance'] and data['guidance'] in call(*words, '--help')
            assert data['hash'] == hashlib.sha256(data['guidance'].encode('utf-8')).hexdigest()
            assert data['bundleVersion'] == root_data['bundleVersion']
            if data['kind'] == 'action':
                actions.append(path)
                assert data['examples'], path
            else:
                assert call(*words) == call(*words, '--help')
            for example in data['examples']:
                # Check parser acceptance without executing or opening anything.
                example_args = [arg for arg in example['argv'] if arg != '--json']
                example_help = json.loads(call(*example_args, '--help', '--json'))
                assert example_help['data']['path'] == path
            pending.extend(child['path'] for child in data['children'])
        assert len(actions) == 20, actions
        for path, data in seen.items():
            assert all(next_path in seen for next_path in data['nextSteps']), path
            # Every explicit help link in bundled instructions must resolve.
            for match in re.finditer(r'synthcad(?:-cli)? ((?:[a-z-]+ )*)--help', data['guidance']):
                linked = match.group(1).strip()
                assert linked in seen, (path, linked)
            assert not re.search(r'synthcad(?:-cli)? (?:docs|skill|capabilities|help)\b', data['guidance']), path
        template = json.loads(call('print', 'profile', '--template'))
        assert template['activeProfile'] in template['profiles']
        assert json.loads(call('print', 'profile', '--template', '--json'))['data'] == template
        version = json.loads(call('--version', '--json'))
        assert version['protocolVersion'] == version['data']['protocolVersion'] == 2
        for old in ('open', 'snapshot', 'state', 'overview', 'docs', 'help', 'capabilities', 'version'):
            error = json.loads(call(old, '--json', exit_code=2))
            assert error['protocolVersion'] == 2 and error['error']['code'] == 'invalid_argument'
            assert 'Use synthcad-cli' in error['error']['message']
        call('review', 'unknown', '--json', exit_code=2)
        call('project', 'open', '--name', 'p', '--project', 'p', '--json', exit_code=2)
        assert sorted(p.name for p in root.iterdir()) == [executable.name], 'Discovery wrote files'
        print(f'PASS domain discovery: {len(seen)} help nodes, 20 actions, UTF-8, hashes, links, migrations, no writes')


if __name__ == '__main__':
    main()
