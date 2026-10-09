"""Native model-worker recovery acceptance; edits only test-owned temporary scenes."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cli', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
cli = a.cli.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
owned = []
env = os.environ.copy()
root_context = tempfile.TemporaryDirectory(prefix='synthcad-recovery-')
root = Path(root_context.name)
env['SYNTHCAD_SESSION_DIR'] = str(root / 'sessions')
env['SYNTHCAD_VIEWER'] = str(cli)
name = 'worker-recovery'

def call(*args, okay=True):
    r = subprocess.run([str(cli), *map(str,args), '--json'], env=env, capture_output=True, timeout=40)
    j = json.loads(r.stdout.decode('utf-8'))
    if okay:
        assert r.returncode == 0 and j['ok'], j
    return j

def state(session=name):
    return call('state', '-s', session)['data']

def wait_status(status, session=name):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        s = state(session)
        if s['status'] == status:
            return s
        time.sleep(.04)
    raise AssertionError(s)

def kill(pid):
    if os.name == 'nt':
        subprocess.run(['taskkill', '/PID', str(pid), '/F'], check=True, capture_output=True)
    else:
        os.kill(pid, signal.SIGKILL)

def worker_pid(parent):
    if os.name == 'nt':
        cmd = f"Get-CimInstance Win32_Process -Filter 'ParentProcessId = {parent}' | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"
        data = subprocess.check_output(['powershell','-NoProfile','-Command',cmd]).decode('utf-8-sig').strip()
        rows = json.loads(data) if data else []
        if isinstance(rows,dict): rows = [rows]
        return next(r['ProcessId'] for r in rows if '--model-worker' in (r['CommandLine'] or ''))
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            fields = path.read_text().rsplit(')',1)[1].split()
            if int(fields[1]) == parent and b'--model-worker' in (path.parent/'cmdline').read_bytes():
                return int(path.parent.name)
        except (OSError,ValueError):
            continue
    raise AssertionError('Owned worker not found')

try:
    scene = root/'design.js'
    good = "export const scene=cube({size:[20,30,4]});"
    scene.write_text(good)
    opened = call('open',scene,'-s',name)
    pid = opened['data']['pid']; owned.append(pid)
    before = wait_status('ready'); revision = before['displayedRevision']
    call('frame','-s',name)
    camera = state()['camera']
    scene.write_text("import './shape.js'; export const scene=cube({size:[20,30,4]});")
    failed = wait_status('failed')
    assert failed['displayedRevision'] == revision and not failed['exportValid']
    assert any(k.endswith('shape.js') for k in failed['dependencies'])
    token = call('revision','-s',name)['data']['revision']
    result = call('wait','-s',name,'--revision',token,okay=False)
    assert result['error']['code'] == 'load_failed' and result['error']['details']['loadFailure']
    call('screenshot',out/'error.png','-s',name,'--replace')
    (root/'shape.js').write_text('export const value=1;')
    recovered = wait_status('ready')
    assert recovered['camera'] == camera
    call('screenshot',out/'recovered.png','-s',name,'--replace')
    revision = recovered['displayedRevision']
    scene.write_text('while(true){}; export const scene=cube({size:[1,1,1]});')
    wait_status('loading')
    start = time.monotonic(); assert state()['status'] == 'loading'; assert time.monotonic()-start < 2
    call('screenshot',out/'loading.png','-s',name,'--replace')
    token = call('revision','-s',name)['data']['revision']
    result = call('wait','-s',name,'--revision',token,'--timeout','30',okay=False)
    assert result['error']['code'] == 'timeout' and state()['status'] == 'loading'
    call('cancel-load','-s',name)
    assert wait_status('failed')['loadFailure']['category'] == 'cancelled'
    call('reload','-s',name,'--evaluation-timeout','150')
    assert wait_status('failed')['loadFailure']['category'] == 'evaluation_timeout'
    call('reload','-s',name,'--evaluation-timeout','10000')
    wait_status('loading'); kill(worker_pid(pid))
    crashed = wait_status('failed')
    assert crashed['loadFailure']['category'] == 'worker_crash' and crashed['loadFailure']['nativeExitCode']
    assert crashed['displayedRevision'] == revision
    call('screenshot',out/'native-error.png','-s',name,'--replace')
    call('reload','-s',name)
    wait_status('loading')
    scene.write_text(good.replace('20','21'))
    final = wait_status('ready')
    assert final['displayedRevision'] != revision and final['exportValid']
    assert call('open',scene,'-s',name)['data']['pid'] == pid
    call('export',out/'recovered.3mf','-s',name,'--allow-warnings','--replace')
    call('export',out/'recovered.stl','-s',name,'--allow-warnings','--replace')
    events = call('events','-s',name,'--after','0')['data']['events']
    assert {'load-loading','load-failed','load-ready'} <= {e['type'] for e in events}
    # Real reduced field from the reported native fault: either it renders or
    # its worker failure is contained with a useful diagnostic and unchanged PID.
    call('open',scene,'-s',name,'--evaluation-timeout','30000')
    field = Path(__file__).resolve().parents[1]/'viewer/tests/agent-fixtures/implicit-field.js'
    scene.write_text(field.read_text(encoding='utf-8'),encoding='utf-8')
    token = call('revision','-s',name)['data']['revision']
    field_result = call('wait','-s',name,'--revision',token,'--timeout','30000',okay=False)
    assert field_result['ok'] or field_result['error']['code'] == 'load_failed', field_result
    field_state = state()
    if not field_result['ok']:
        assert field_state['loadFailure']['category'] == 'worker_crash'
        assert field_state['loadFailure']['context']['operation'] == 'levelSet'
        assert field_state['displayedRevision'] == final['displayedRevision']
    assert call('open',scene,'-s',name)['data']['pid'] == pid
    (out/'implicit-field-result.json').write_text(json.dumps(field_result,indent=2))
    scene.write_text(good);wait_status('ready')
    other = root/'invalid.js'; other.write_text("throw Error('First load failed');")
    opened = call('open',other,'-s','failed-first'); owned.append(opened['data']['pid'])
    first = wait_status('failed','failed-first')
    assert first['parts'] == [] and first['displayedRevision'] == ''
    call('screenshot',out/'first-load-error.png','-s','failed-first','--replace')
    (out/'results.json').write_text(json.dumps({'viewerPid':pid,'crash':crashed['loadFailure'],'recoveredRevision':final['displayedRevision'],'result':'passed'},indent=2))
    print('PASS responsive viewer, native crash, imports, timeout, cancellation, superseding, recovery, events, export and first-load error')
finally:
    for pid in owned:
        try: kill(pid)
        except (OSError,subprocess.SubprocessError): pass
    time.sleep(.2)
    root_context.cleanup()
