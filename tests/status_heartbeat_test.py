#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
spec=importlib.util.spec_from_file_location('watch',Path(__file__).resolve().parents[1]/'tools/thc_error_watch.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
with tempfile.TemporaryDirectory() as td:
 path=Path(td)/'state.json';watch=w.Watcher(path)
 status={'running':True,'processIdentity':'boot:1:100','processStartedAt':1,'jobs':[]}
 watch.process_status(status,100)
 watch.state['invite'].update(valid=True,code='ABCD-1234',processIdentity=status['processIdentity'])
 watch.status_heartbeat(status,100);assert not watch.state['pending']
 watch.status_heartbeat(status,2799);assert not watch.state['pending']
 watch.status_heartbeat(status,2800);assert len(watch.state['pending'])==1
 assert watch.state['pending'][0]['content']=='🟢 **The High Celestial is ONLINE!**\n🔑 **Server invite code**\n`ABCD-1234`'
 watch.save();watch=w.Watcher(path);watch.process_status(status,2810);watch.status_heartbeat(status,2810);assert len(watch.state['pending'])==1
 # A Discord failure retains one summary and its retry deadline; no backlog.
 watch.deliver({'status':'test'},2810,lambda *_:(429,5000));due=watch.state['pending'][0]['due']
 watch.status_heartbeat(status,5500);assert len(watch.state['pending'])==1 and watch.state['pending'][0]['due']==due
 # Important events in the lane go before a routine summary, without changing any content.
 watch.queue('🔑 important fresh code',5500,'status','invite');sent=[]
 watch.deliver({'status':'test'},5500,lambda _,payload:(sent.append(payload) or (200,0)))
 assert sent[0]['content']=='🔑 important fresh code' and sent[0]['allowed_mentions']=={'parse':[]}
 # No stale running/code posts while controller observations are unavailable.
 watch.can_send_invites=False;sent.clear();watch.deliver({'status':'test'},10000,lambda _,payload:(sent.append(payload) or (200,0)));assert not sent
 # A code change cancels the old-code summary and starts a fresh 45-minute timer.
 watch.state['invite'].update(code='EFGH-5678');watch.status_heartbeat(status,10000);assert not watch.state['pending']
 watch.status_heartbeat(status,12700);assert 'EFGH-5678' in watch.state['pending'][0]['content']
 watch.process_status({'running':False,'processIdentity':'','jobs':[]},12701)
 watch.status_heartbeat({'running':False,'processIdentity':'','jobs':[]},12701)
 assert not any(item.get('kind')=='status_heartbeat' for item in watch.state['pending'])
 # No periodic offline or code-less message was requested.
 watch.status_heartbeat({'running':False,'processIdentity':'','jobs':[]},15401)
 assert not any(item.get('kind')=='status_heartbeat' for item in watch.state['pending'])
 watch.state['invite'].update(valid=False);watch.status_heartbeat(status,16000)
 assert not any(item.get('kind')=='status_heartbeat' for item in watch.state['pending'])
 print('PASS: 45-minute online/current-code repost, restart persistence, no extra offline posts, stale-code removal, retry coalescing, important-event priority and no mentions; no network calls')
