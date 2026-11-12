#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile

spec=importlib.util.spec_from_file_location('errors',Path(__file__).resolve().parents[1]/'tools/thc_error_watch.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
with tempfile.TemporaryDirectory(prefix='thc-errors-') as folder:
 p=Path(folder);state=p/'watch.json';log=p/'RSDragonwilds.log'
 log.write_text('[old] LogCore: Error: historical error\n')
 watch=w.Watcher(state);watch.scan('Game log',log,100);watch.save();assert not watch.state['pending']
 with log.open('a') as stream:
  stream.write('[2026.10.26-06.01.01:001][123]LogCore: Fatal error: private-secret Bearer token-secret\n')
  stream.write('[2026.10.26-06.01.02:002][124]LogCore: Fatal error: private-secret Bearer token-secret\n')
  stream.write('LogWeather: Warning: ordinary cache warning\n')
  stream.write('LogNormal: routine poll\n')
  stream.write('LogCore: Fatal error: pending')
 watch.scan('Game log',log,105,('private-secret',));assert len(watch.state['pending'])==1
 assert 'private-secret' not in watch.state['pending'][0]['content'] and 'token-secret' not in watch.state['pending'][0]['content']
 watch.save();watch=w.Watcher(state)
 with log.open('a') as stream:stream.write(' fatal line\n')
 watch.scan('Game log',log,110);assert len(watch.state['pending'])==2
 watch.flush_groups(405);assert len(watch.state['pending'])==3 and 'Repeated **1**' in watch.state['pending'][-1]['content']
 watch.save();watch=w.Watcher(state);assert len(watch.state['pending'])==3
 watch.queue('recovered',405,'status');watch.save();called=[]
 def throttled(url,payload):
  called.append((url,payload));return (429,30) if url=='admin' else (204,0)
 watch.deliver({'admin':'admin','status':'status'},405,throttled)
 assert len(called)==2 and all(x[1]['allowed_mentions']=={'parse':[]} for x in called)
 assert len(watch.state['pending'])==3 and watch.state['pending'][0]['due']==435
 watch=w.Watcher(state);watch.deliver({'admin':'admin'},434,lambda *_: (_ for _ in ()).throw(AssertionError('early retry')))
 watch.deliver({'admin':'admin'},435,lambda *_:(204,0));assert len(watch.state['pending'])==2
 watch.deliver({'admin':'admin'},436,lambda *_:(404,0));assert watch.state['pending'][0]['due']>=736
 # Rename rotation drains unconsumed content, then follows the new log.
 with log.open('a') as stream:stream.write('LogCore: Fatal error: old inode tail')
 log.rename(p/'RSDragonwilds-backup.log');log.write_text('LogCore: Fatal error: new boot error\n')
 before=len(watch.state['pending']);watch.scan('Game log',log,500);assert len(watch.state['pending'])==before+2
 # In-place truncation, fresh partial line, warning opt-in and credential redaction.
 log.write_text('LogCore: Fatal error: truncated\n');watch.scan('Game log',log,505)
 assert any('truncated' in x['content'] for x in watch.state['pending'])
 # Detect copy-truncate/regrow beyond the old offset using a hashed boundary.
 log.write_text('LogCore: Fatal error: rewritten with a longer line than before, same inode\n')
 watch.scan('Game log',log,506)
 assert any('rewritten with a longer' in x['content'] for x in watch.state['pending'])

 warnings=w.Watcher(p/'warnings.json');warnings.line('game','LogWeather: Warning: example',0);warnings.line('game','LogWeather: Error: Trying to exceed MaxWeatherCacheSize',0);warnings.line('game','LogOther: Warning: Error: quoted example',0);assert not warnings.state['pending'];warnings.line('game','LogCore: Fatal error!',1);assert len(warnings.state['pending'])==1
 text=w.redact('Password=abc https://discord.com/api/webhooks/123/secret Bearer xyz')
 assert 'abc' not in text and '/123/secret' not in text and 'xyz' not in text
 # Completed stop jobs between polls still identify planned downtime.
 watch.process_status({'running':True,'jobs':[]},600)
 before_planned=len(watch.state['pending'])
 watch.process_status({'running':False,'jobs':[{'id':'job','action':'stop','state':'complete'}]},605)
 watch.process_status({'running':False,'jobs':[]},610);watch.process_status({'running':False,'jobs':[]},615)
 assert len(watch.state['pending'])==before_planned
 watch.process_status({'running':True,'jobs':[]},620);assert watch.state['pending'][-1]['lane']=='status'
 for at in (900,905,910):watch.process_status({'running':False,'jobs':[]},at)
 assert 'no longer observed' in watch.state['pending'][-1]['content']

 # A completed restart must not hide a new crash during its old grace window.
 grace=w.Watcher(p/'grace.json');grace.process_status({'running':True,'processIdentity':'old','jobs':[]},1000)
 grace.process_status({'running':True,'processIdentity':'new','jobs':[{'id':'restart','action':'restart','state':'complete'}]},1005)
 assert grace.state['process']['expectedUntil']==0
 for at in (1010,1015,1020):grace.process_status({'running':False,'processIdentity':'','jobs':[]},at)
 assert len(grace.state['pending'])==1 and 'Server process stopped' in grace.state['pending'][0]['content']
 # Queue pressure must not advance a log cursor past an undelivered error.
 blocked=w.Watcher(p/'blocked.json');log.write_text('');blocked.scan('game',log,0)
 blocked.state['pending']=[{}]*1000;log.write_text('LogCore: Fatal error: keep cursor\n')
 try:blocked.scan('game',log,5);raise AssertionError('full queue silently accepted')
 except ValueError:pass
 assert blocked.state['files']['game']['offset']==0
 blocked.state['pending']=[];blocked.scan('game',log,10);assert len(blocked.state['pending'])==1
 # Private routing uses uint64 strings; preexisting thread route preserved when unset.
 url='https://discord.com/api/webhooks/123/token?thread_id=1111111111111111111'
 routed=w.route(url,'1560963849358934087');assert 'thread_id=1560963849358934087' in routed and routed.count('thread_id=')==1
 assert 'thread_id=1111111111111111111' in w.route(url,'')
 for invalid in ['http://discord.com/api/webhooks/123/token','https://example.com/api/webhooks/123/token']:
  try:w.route(invalid,'');raise AssertionError('bad webhook accepted')
  except ValueError:pass
 try:w.route(url,'99999999999999999999');raise AssertionError('overflow ID accepted')
 except ValueError:pass

 # Owner-confirmed Linux category/field shape; bind codes to the current process.
 invite=w.Watcher(p/'invite.json');invite_log=p/'invite-game.log';invite_log.write_text('LogNetSessionSettings: Setting ["JoinCode"] written with key[xz] value[OLD1-OLD2]\n')
 invite.process_status({'running':True,'processIdentity':'boot:100:200','jobs':[]},1000)
 invite.scan('Game log',invite_log,1000);assert not invite.state['invite']['valid']
 with invite_log.open('a') as stream:
  stream.write('Log file open, 10/26/26 06:00:00\n')
  stream.write('[2026.10.26-06.00.01:123][115]LogNetSessionSettings: Setting ["JoinCode"] written with key[xz] value[ABCD-1234]\n')
  stream.write('[2026.10.26-06.00.02:123][137]LogNetSessionSettings: Setting ["JoinCode"] written with key[xz] value[ABCD-1234]\n')
 invite.scan('Game log',invite_log,1005);assert invite.state['invite']['code']=='ABCD-1234'
 assert len([x for x in invite.state['pending'] if x.get('kind')=='invite'])==1
 invite.save();invite=w.Watcher(p/'invite.json');invite.scan('Game log',invite_log,1010)
 assert len(invite.state['pending'])==1 and invite.state['invite']['valid']
 # Unknown categories/chat echoes, malformed and ambiguous fields cannot replace it.
 for line in ['LogChat: ["JoinCode"] value[FAKE-CODE]', 'LogNetSessionSettings: ["JoinCode"] value[short]', 'LogNetSessionSettings: ["JoinCode"] value[FAKE-CODE] value[MORE-FAKE]']:
  invite.line('Game log',line,1015)
 assert invite.state['invite']['code']=='ABCD-1234'
 # New process invalidates the old code and cancels its unsent notification.
 invite.process_status({'running':True,'processIdentity':'boot:101:300','jobs':[]},1020)
 assert not invite.state['invite']['valid'] and not invite.state['pending']
 invite.line('Game log','LogNetSessionSettings: ["JoinCode"] value[NEW1-NEW2]',1025)
 assert invite.state['invite']['code']=='NEW1-NEW2'
 invite.process_status({'running':False,'processIdentity':'','jobs':[]},1030)
 assert not invite.state['invite']['valid'] and not invite.state['pending']

 # Late-start catch-up accepts only timestamped current-process evidence.
 from datetime import datetime,timezone
 born=datetime(2026,10,26,5,0,0,tzinfo=timezone.utc).timestamp()
 catch=w.Watcher(p/'catchup.json');catchlog=p/'catchup.log'
 catchlog.write_text('[2026.10.26-04.00.00:000][100]LogNetSessionSettings: Setting ["JoinCode"] written with key[xz] value[OLD1-OLD2]\n[2026.10.26-05.00.05:000][115]LogNetSessionSettings: Setting ["JoinCode"] written with key[xz] value[LATE-CODE]\n')
 catch.process_status({'running':True,'processIdentity':'boot:200:500','processStartedAt':born,'jobs':[]},born+10)
 catch.scan('Game log',catchlog,born+10)
 assert catch.state['invite']['code']=='LATE-CODE' and len(catch.state['pending'])==1
 assert catch.state['pending'][0]['content']=='🔑 **Server invite code**\n`LATE-CODE`'
 assert not catch.state['invite']['catchup']
 catch.process_status({'running':True,'processIdentity':'boot:201:600','processStartedAt':born+20,'jobs':[]},born+25)
 catch.scan('Game log',catchlog,born+25)
 assert not catch.state['invite']['valid'] and not catch.state['pending']

 # Audit records are a separate allowed source, not general information logs.
 audit=w.Watcher(p/'audit-state.json');auditlog=p/'controller-audit.log';audit.scan('Admin audit',auditlog,0)
 entry={'schema':1,'id':'a'*32,'actor':'shared admin credential','action':'/api/thc/console','outcome':'HTTP 200','details':{'command':'domCommand','note':'private-secret'}}
 auditlog.write_text(json.dumps(entry)+'\n');audit.scan('Admin audit',auditlog,5,('private-secret',))
 assert len(audit.state['pending'])==1 and 'Admin audit' in audit.state['pending'][0]['content']
 assert audit.state['pending'][0]['content'].startswith('🛡️ **Admin audit**') and '[THC observer #' not in audit.state['pending'][0]['content']
 assert '**Command:** domCommand' in audit.state['pending'][0]['content']
 assert 'private-secret' not in audit.state['pending'][0]['content']
 audit.line('Admin audit',json.dumps(entry),10);assert len(audit.state['pending'])==1
 audit.line('Game log','LogCore: Display: routine information',15);assert len(audit.state['pending'])==1
 for ident,action,outcome,delta in [('b','control:stop','accepted',0),('c','control:stop','complete',1),('d','/api/thc/save','accepted (completion separate)',0),('e','control:start','failed',1),('f','/api/thc/save','HTTP 403',1)]:
  before=len(audit.state['pending'])
  audit.line('Admin audit',json.dumps({**entry,'id':ident*32,'action':action,'outcome':outcome}),20)
  assert len(audit.state['pending'])==before+delta
  audit.line('Admin audit',json.dumps({**entry,'id':ident*32,'action':action,'outcome':outcome}),21)
  assert len(audit.state['pending'])==before+delta

 # Labelled credentials identify jobs; native-queued copies stay private.
 named=w.Watcher(p/'named-audit.json')
 identity={'id':'minghoo-id','label':'Minghoo','role':'admin'}
 named.line('Admin audit',json.dumps({**entry,'id':'1'*32,'actor':identity,'action':'control:backup','outcome':'accepted'}),30)
 assert not named.state['pending']
 named.line('Admin audit',json.dumps({**entry,'id':'2'*32,'actor':identity,'action':'control:backup','outcome':'complete'}),31)
 assert len(named.state['pending'])==1 and 'Minghoo (admin)' in named.state['pending'][0]['content'] and 'minghoo-id' in named.state['pending'][0]['content']
 named.line('Admin audit',json.dumps({**entry,'id':'3'*32,'actor':identity,'details':{'nativeAuditQueued':True}}),32)
 assert len(named.state['pending'])==1
 named.line('Admin audit',json.dumps({**entry,'id':'4'*32,'actor':identity,'action':'/api/thc/save','outcome':'accepted (completion separate)','details':{}}),33)
 assert len(named.state['pending'])==2

 # Ordinary game errors supplied by the owner must not be forwarded.
 quiet=w.Watcher(p/'quiet.json')
 ordinary=[
  'DominionLog: Error: DropItem() : Failed to spawn and launch item res_ground_kalphite_wing.',
  'DominionContentRestrictionSubsystem: Error: Identity Interface not valid during cleanup.',
  'LogHoudiniEngineRuntime: Error: FUnrealObjectInputManager not initialized correctly!!!!!!!!!!!!!!!!!!!!',
  'LogHoudiniEngineRuntime: Error: Plugin will not function correctly !!!!!!!!!!!!!!!!!!!!',
  'DominionContentRestrictionSubsystem: Error: No Online Subsystem found for this platform.',
  'DominionAchievementSubsystem: Error: Failed to fetch relevant OnlineSubsystem',
  'LogBinkMoviePlayer: Error: UBinkMediaPlayer::Open: Failed! BinkPluginOpen failed. Invalid bnk.',
  'LogStreaming: Error: CreateExport: Could not find class object for DT_VitalShield',
  'LogNavigation: Error: Navmesh bounds are too large! Limiting requested tiles count',
  'DominionBuildingActorLog: Error: Not enough Materials supplied',
  'DominionLog: Error: Local Player Character not found after 10 iterations.',
  'LogWeather: Error: Trying to exceed MaxWeatherCacheSize',
 ]
 for line in ordinary:quiet.line('Game log',line,100)
 assert not quiet.state['pending'] and not quiet.state['groups']
 for line in ['LogCore: Fatal error!', 'Unhandled Exception: SIGSEGV', 'Caught signal 11 Segmentation fault', 'Assertion failed: index < count', 'LogMemory: Error: Out of memory allocating 123456 bytes']:
  quiet.line('Game log',line,100)
 assert len(quiet.state['pending'])==5
 # Upgrade migration removes old ordinary alert/repeat backlog but preserves
 # audits, invite codes, process-loss alerts and real fatal errors.
 migration=p/'migration.json';old=w.Watcher(migration)
 old.state['pending']=[
  {'id':1,'content':'Game log: LogHoudiniEngineRuntime: Error: plugin not initialized','lane':'admin','due':0,'attempt':0},
  {'id':2,'content':'🚨 **Game error**\nLogNavigation: Error: Navmesh bounds too large','lane':'admin','due':0,'attempt':0},
  {'id':3,'content':'Admin audit — shared administration: control:stop — complete','lane':'admin','due':0,'attempt':0},
  {'id':4,'content':'🛡️ **Admin audit**\n**Action:** Start server','lane':'admin','due':0,'attempt':0},
  {'id':5,'content':'🔑 **Server invite code**\n`ABCD-1234`','lane':'status','due':0,'attempt':0,'kind':'invite'},
  {'id':6,'content':'🔴 **Server process stopped**','lane':'admin','due':0,'attempt':0},
  {'id':7,'content':'Game log: LogCore: Fatal error!','lane':'admin','due':0,'attempt':0},
 ]
 old.state['groups']={'ordinary':{'text':ordinary[0],'source':'Game log','count':12,'last':0},'fatal':{'text':'LogCore: Fatal error!','source':'Game log','count':2,'last':0}}
 old.save();new=w.Watcher(migration)
 assert [x['id'] for x in new.state['pending']]==[3,4,5,6,7]
 assert list(new.state['groups'])==['fatal'] and new.state['filteredLogAlerts']==2
 new.flush_groups(400);assert new.state['pending'][-1]['content'].startswith('🚨 **Critical game error**')
 new.save();again=w.Watcher(migration);assert again.state['filteredLogAlerts']==2
 # Corrupt state is preserved rather than reset.
 state.write_text('broken')
 try:w.Watcher(state);raise AssertionError('corrupt state reset')
 except ValueError:pass
 assert state.read_text()=='broken'
print('PASS: independent error capture, no historical flood, partial lines, rotation/truncation, redaction, repeat summaries, durable retry/429/rejected-webhook retention, independent lanes, planned/unplanned process transitions, queue backpressure, route validation and corrupt-state preservation (no Discord calls)')
