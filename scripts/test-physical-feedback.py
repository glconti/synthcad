#!/usr/bin/env python3
"""Optional physical-feedback flow through public files and CLI; no real prints.

Uses a copied fixture and synthetic user reports, never personal models. Run on
Linux under Xvfb or another X display. --keep-temp retains the evidence.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('session_tests',ROOT/'scripts/test-agent-session.py')
session=importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)

def require(value,message):
    if not value: raise RuntimeError(message)

def run(args):
    folder=Path(tempfile.mkdtemp(prefix='synthcad-feedback-'))
    print(f'Physical-feedback acceptance evidence: {folder}',flush=True)
    project=folder/'Campione città 日本'
    shutil.copytree(session.FIXTURES/'physical-feedback',project)
    config,parts=project/'synthcad.json',project/'parts.js'
    metadata=json.loads(config.read_text(encoding='utf-8'))
    client=session.Client(session.discover_executable(args.cli),Path(args.viewer).resolve() if args.viewer else None,40)
    client.session_dir,client.trace_path=folder/'sessions',folder/'cli-trace.jsonl'
    pids=[]
    def call(*cmd,**kw):return client.call(*cmd,'-s','feedback',**kw)
    def token():return session._revision(call('revision'))
    def settle(previous=None):
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            response=call('revision',expected_code=(0,11))
            if response['ok']:
                revision=session._revision(response)
                if previous is None or revision!=previous:
                    return call('wait','--revision',revision,'--timeout','25000')
            time.sleep(.1)
        raise RuntimeError('Revision did not settle')
    def write_metadata():
        before=token()
        config.write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')
        settle(before)
    def overview():return call('overview')['data']
    def view(name):call('view',name);settle()
    def basis(name):
        entry=next(v for v in overview()['views'] if v['id']==name)
        require(entry['loaded'] and entry['sourceCurrent'],'Requested basis is not current')
        return {'view':name,'modelRevision':entry['modelRevision'],'profileRevision':None}
    def export(name):
        response=call('export',str(folder/name),'--allow-warnings','--expect-revision',call('snapshot')['data']['displayedRevision'])['data']
        require(response['history']['saved'],'Export receipt was not retained')
        return response['record']
    def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    try:
        opened=call('open',str(project),'--hidden')
        pids.extend(pid for pid,_ in session._pid_records(opened));require(pids,'Missing owned viewer PID')
        settle();view('plate')
        initial=overview()
        require(initial['evidence']==[] and initial['compatibilityChanges']==[],'Samples unexpectedly required or fabricated')
        full=export('full-before.3mf');old_basis=basis('plate')
        require(set(full['partIds'])=={'socket-1','pin-1'},'Full export leaked sample or external reference')
        require(overview()['evidence']==[],'Export inferred physical evidence')
        full_hash=sha(folder/'full-before.3mf')
        metadata['evidence']=[{'id':'fit-1','text':'Synthetic optional fit sample proposed; no physical test performed.',
            'stage':'proposed','sampleView':'fit-sample','sourcePartIds':['socket','pin'],
            'partIds':['coupon-1','pin-1']}]
        write_metadata()
        require(overview()['evidence'][0]['sampleViewStatus']=='available','Known unvisited sample view cannot be opened')
        view('fit-sample');sample_basis=basis('fit-sample')
        metadata['evidence'][0]['basis']=sample_basis;write_metadata()
        require(basis('fit-sample')==sample_basis,'Metadata edit invalidated its own model basis')
        sample=export('sample-before.stl')
        require(set(sample['partIds'])=={'coupon-1','pin-1'},'Sample did not reference intended reusable parts')
        require(overview()['evidence'][0]['stage']=='proposed','Sample export implied a physical print')
        sample_hash=sha(folder/'sample-before.stl')
        metadata['evidence'][0]['stage']='printed'
        metadata['evidence'][0]['text']='Synthetic acceptance report: user says the sample was printed.'
        write_metadata();require(overview()['evidence'][0]['stage']=='printed','Explicit printed report lost')
        metadata['evidence'][0].update(stage='tested',observation={
            'kind':'fit','result':'failed','reportedBy':'Simulated acceptance-test user',
            'details':'Synthetic report: the pin would not seat. Not an actual physical test.',
            'conditions':'Example hand fit; printer/material unspecified; no strength conclusion.'})
        write_metadata()
        report=overview()['evidence'][0]
        require(report['stage']=='tested' and report['observation']['result']=='failed' and report['origin']=='authored','Reported test was lost or promoted to verified evidence')
        view('plate');before=token()
        source=parts.read_text(encoding='utf-8')
        require('clearance = 0.1' in source,'Fixture edit marker missing')
        parts.write_text(source.replace('clearance = 0.1','clearance = 0.25'),encoding='utf-8');settle(before)
        revised_basis=basis('plate')
        require(revised_basis!=old_basis,'Geometry edit did not change model revision')
        stale=overview()['evidence'][0]
        require(stale['freshness']=='stale' and stale['stage']=='tested','Revision edit must stale basis without changing authored physical stage')
        metadata['compatibilityChanges']=[{
            'id':'clearance-v2','text':'Synthetic design decision: enlarge socket clearance; retain the unchanged pin.',
            'status':'requires-reprint','basis':revised_basis,'previousBasis':old_basis,
            'partIds':['socket-1','pin-1'],'reprintPartIds':['socket-1'],'evidenceIds':['fit-1']}]
        write_metadata();change=overview()['compatibilityChanges'][0]
        require(change['freshness']=='current' and change['reprintPartIds']==['socket-1'] and change['evidenceStatus']=='available','Compatibility/reprint decision was not linked')
        require(change['previousBasis']==old_basis,'Previous basis was rewritten')
        updated=export('full-after.3mf')
        require(updated['sha256']!=full['sha256'],'Revised geometry export did not change')
        require(sha(folder/'full-before.3mf')==full_hash and sha(folder/'sample-before.stl')==sample_hash,'Iteration overwrote previous outputs')
        history={r['id']:r for r in call('export-history')['data']['records']}
        require(history[full['id']]['freshness']=='stale','Old generated export was not marked stale')
        metadata['evidence'][0]['stage']='superseded';write_metadata()
        require(overview()['evidence'][0]['stage']=='superseded','Explicit superseded report not retained')
        metadata['compatibilityChanges'][0]['reprintPartIds']=['unrelated']
        write_metadata();invalid=overview()
        require(invalid['compatibilityChanges']==[] and invalid['errors'] and invalid['evidence'],'Invalid reprint subset did not retain valid siblings')
        require(call('state')['data']['exportValid'],'Optional record error blocked otherwise valid geometry')
        metadata['compatibilityChanges'][0]['reprintPartIds']=['socket-1'];write_metadata()
        (folder/'final-overview.json').write_text(json.dumps(overview(),ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS optional full export, sample references, explicit reports/stages, revision freshness, reprint decisions and preserved outputs',flush=True)
    finally:
        for pid in set(pids):
            try:session._terminate_owned_pid(pid)
            except OSError:
                if os.name=='nt' or (Path(f'/proc/{pid}/stat').exists() and Path(f'/proc/{pid}/stat').read_text().split()[2]!='Z'):raise
        if not args.keep_temp:shutil.rmtree(folder)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli');parser.add_argument('--viewer');parser.add_argument('--keep-temp',action='store_true')
    run(parser.parse_args())
