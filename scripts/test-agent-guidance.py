"""Verify standalone stdout guidance without a viewer, checkout or installed skills."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    source = args.cli.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='synthcad-guides-città-') as temporary:
        root = Path(temporary)
        executable = root / source.name
        shutil.copy2(source, executable)
        env = dict(os.environ, SYNTHCAD_VIEWER=str(root / 'no-viewer-here'),
                   SYNTHCAD_SESSION_DIR=str(root / 'no-sessions-here'))

        def call(*arguments, exit_code=0):
            result = subprocess.run([str(executable), *arguments], cwd=root, env=env,
                                    capture_output=True, timeout=10)
            assert result.returncode == exit_code, (arguments, result.returncode, result.stderr, result.stdout)
            assert not result.stderr, result.stderr
            return result.stdout.decode('utf-8').replace('\r\n', '\n')

        help_text = call('--help')
        assert call() == help_text
        for area in ('Getting started', 'Modeling', 'Printing & assembly', 'Plates & handoff', 'Agent review'):
            assert area in help_text, area
        assert 'docs [AREA]' in call('help', 'docs')
        assert 'profile [--template]' in help_text and 'overview' in help_text
        fragment = json.loads(call('profile', '--template'))
        assert fragment['activeProfile'] in fragment['profiles']
        profile = fragment['profiles'][fragment['activeProfile']]
        assert profile['buildVolume'] is None and profile['nozzleDiameter'] is None
        envelope = json.loads(call('profile', '--template', '--json'))
        assert envelope['ok'] and envelope['command'] == 'profile' and envelope['data'] == fragment
        call('profile', '--template', '--expect-revision', 'stale', exit_code=2)
        index = json.loads(call('docs', '--json'))
        assert index['ok'] and index['command'] == 'docs'
        topics = index['data']['topics']
        assert len(topics) == 14
        assert 'bundleVersion' in index['data']
        for entry in topics:
            topic = entry['topic']
            assert topic in help_text, topic
            response = json.loads(call('docs', topic, '--json'))
            data = response['data']
            assert response['ok'] and data['topic'] == topic
            assert data['bundleVersion'] == index['data']['bundleVersion']
            assert data['hash'] == hashlib.sha256(data['content'].encode('utf-8')).hexdigest()
            assert call('docs', topic) == data['content'], topic
            assert not Path(data['source']).is_absolute()
        error = json.loads(call('docs', 'not-a-topic', '--json', exit_code=12))
        assert not error['ok'] and error['error']['code'] == 'not_found'
        assert 'synthcad docs' in error['error']['message']
        call('docs', 'start', 'extra', exit_code=2)
        assert sorted(path.name for path in root.iterdir()) == [executable.name], 'Discovery wrote files'
    print('PASS stdout-only guides: standalone CLI, grouped help, 14 complete topics, UTF-8, hashes, errors, no writes')


if __name__ == '__main__':
    main()
