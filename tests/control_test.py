import importlib.util
import json
import os
from pathlib import Path
import socket
import tempfile
import threading
import time
import zipfile
spec=importlib.util.spec_from_file_location('control',Path(__file__).resolve().parents[1]/'tools/thc_control.py')
control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory)/'server';root.mkdir();saves=root/'Saved/SaveGames';saves.mkdir(parents=True);(saves/'world.sav').write_bytes(b'known-world-save')
 log=root/'server.log';log.write_text('WorldPassword=secret\nAuthorization: Bearer secret-token\nhttps://discord.com/api/webhooks/123/token\nnormal event\n')
 ini=root/'DedicatedServer.ini';ini.write_text('[Server]\nServerName=Before\nMaxPlayers=4\nWorldPassword=secret\nOtherSetting=unchanged\n')
 game=root/'fake-game';game.write_text('test fixture only')
 config={'server_root':str(root),'save_directory':'Saved/SaveGames','backup_directory':str(Path(directory)/'backups'),'log_file':'server.log','settings_file':'DedicatedServer.ini','mode':'disabled','game_executable':str(game)}
 c=control.Controller(config);assert c.status()['running'] is False
 c.backup();rows=c.list_backups();assert len(rows)==1 and rows[0]['files']==1
 (saves/'world.sav').write_bytes(b'changed-world');c.restore(rows[0]['id']);assert (saves/'world.sav').read_bytes()==b'known-world-save';assert list(saves.parent.glob('SaveGames.thc-before-restore-*'))
 assert c.settings()['WorldPassword'] is True and 'secret' not in json.dumps(c.settings())
 c.update_settings({'ServerName':'After','MaxPlayers':8});assert 'OtherSetting=unchanged' in ini.read_text();assert c.settings()['ServerName']=='After'
 original=ini.read_text()
 for values in [{'ServerName':'bad\nWorldPassword=injected'},{'UnknownKey':'value'}]:
  try:c.update_settings(values);raise AssertionError('unsafe settings accepted')
  except ValueError:pass
 assert ini.read_text()==original
 lines='\n'.join(c.logs());assert 'secret-token' not in lines and '/123/token' not in lines and 'normal event' in lines
 # Even a validly checksummed archive must not escape the configured save root.
 path=c.backups/(rows[0]['id']+'.zip')
 with zipfile.ZipFile(path,'w') as z:z.writestr('../outside.txt','bad');z.writestr('__THC_MANIFEST__.json','{}')
 metadata=json.loads((c.backups/(rows[0]['id']+'.json')).read_text());metadata['sha256']=control.digest(path);control.atomic_json(c.backups/(rows[0]['id']+'.json'),metadata)
 try:c.restore(rows[0]['id']);raise AssertionError('traversal accepted')
 except ValueError:pass
 assert not (root/'Saved/outside.txt').exists() and (saves/'world.sav').read_bytes()==b'known-world-save'
 # Owned-process control uses a tiny local fixture, never a real game/server.
 import sys
 owned={**config,'mode':'owned_process','command':[sys.executable,'-u','-c','import time; print("fixture ready",flush=True); time.sleep(120)']}
 managed=control.Controller(owned)
 try:
  managed._start();assert managed.running()
  try:managed._start();raise AssertionError('duplicate process allowed')
  except ValueError:pass
  try:managed.backup();raise AssertionError('live backup allowed')
  except ValueError:pass
  managed._stop();assert not managed.running()
 finally:
  if managed.process and managed.process.poll() is None:managed.process.terminate();managed.process.wait(timeout=5)
 # Exercise the actual peer-checked Unix protocol over a temporary local socket.
 sock=Path(directory)/'control.sock'
 with control.Server(str(sock),control.Handler) as server:
  server.controller=c;thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  try:
   with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as peer:
    peer.connect(str(sock));peer.sendall(b'{"action":"status"}\n');result=json.loads(peer.recv(65536));assert result['ok'] and result['mode']=='disabled'
   with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as peer:
    peer.connect(str(sock));peer.sendall(b'{"action":"run_shell","command":"unsafe"}\n');result=json.loads(peer.recv(65536));assert not result['ok']
  finally:server.shutdown();thread.join()
 # The separate dashboard remains authenticated and usable with the game API down.
 api_ini=root/'api.ini';api_ini.write_text('[API]\nBearerToken=owner-test-secret\nPort=8081\n')
 (root/'thc').mkdir(exist_ok=True);(root/'thc/access.json').write_text(json.dumps([{'id':'v','label':'Viewer','role':'viewer','token':'viewer-test-secret'},{'id':'minghoo-id','label':'Minghoo','role':'admin','token':'minghoo-private-secret'}]))
 dashboard=root/'dashboard.html';dashboard.write_text('<html>THC fixture</html>')
 c.audit_api=api_ini
 proxy=control.DashboardProxy(c,{**config,'api_config':'api.ini','dashboard_file':str(dashboard)})
 class Offline:
  def open(self,*args,**kwargs):raise control.urllib.error.URLError('fixture offline')
 proxy.opener=Offline()
 import http.server,urllib.request,urllib.error
 web=http.server.ThreadingHTTPServer(('127.0.0.1',0),control.DashboardHandler);web.proxy=proxy
 thread=threading.Thread(target=web.serve_forever,daemon=True);thread.start()
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
 def request(path,token='owner-test-secret',body=None):
  req=urllib.request.Request('http://127.0.0.1:'+str(web.server_port)+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
  try:
   with opener.open(req,timeout=3) as response:return response.status,json.load(response)
  except urllib.error.HTTPError as error:return error.code,json.load(error)
 try:
  assert request('/api/thc/control',token='wrong')[0]==401
  assert request('/api/thc/control',token='viewer-test-secret')[0]==403
  assert request('/api/thc/access/me',token='viewer-test-secret')[1]['role']=='viewer'
  assert request('/api/health')[1]['status']=='offline'
  assert request('/api/thc/status')[1]['gameOffline'] is True
  assert request('/api/thc/control')[1]['ok'] is True
  code,job=request('/api/thc/control',body={'action':'backup'});assert code==200 and job['job']
  for _ in range(40):
   status=request('/api/thc/control')[1];matching=next(row for row in status['jobs'] if row['id']==job['job'])
   if matching['state']!='running':break
   time.sleep(.05)
  assert matching['state']=='complete',matching
  assert request('/api/thc/control',body={'action':'run_shell','command':'nope'})[0]==409
  # Dashboard commands and asynchronous jobs have private, redacted audit records.
  assert request('/api/thc/console',body={'command':'domWeather 1'})[0]==503
  assert request('/api/thc/settings',body={'adminWebhook':'https://discord.com/api/webhooks/123/sensitive','discordEnabled':True})[0]==503
  assert request('/api/broadcast',body={'message':'owner-test-secret','sender':'THC'})[0]==503
  native_forward=proxy.forward
  def queued(*args):
   args[3]['nativeAuditQueued']=True
   return 202,b'{\"id\":1,\"state\":\"queued\"}'
  proxy.forward=queued
  assert request('/api/thc/recheck',body={})[0]==202
  proxy.forward=native_forward
  assert any(json.loads(line)['action']=='/api/thc/recheck' and json.loads(line)['details'].get('nativeAuditQueued') for line in c.audit_path.read_text().splitlines())
  records=[json.loads(line) for line in c.audit_path.read_text().splitlines()]
  assert any(row['action']=='control:backup' and row['outcome']=='complete' for row in records)
  assert any(row['details'].get('command')=='domWeather 1' for row in records)
  assert 'owner-test-secret' not in c.audit_path.read_text() and '/123/sensitive' not in c.audit_path.read_text()
  assert c.audit_path.stat().st_mode & 0o777==0o600
  assert control.audit_details('/api/thc/console',{'command':'setServerPassword private'})['command']=='[sensitive command arguments redacted]'

  assert request('/api/thc/access/me',token='minghoo-private-secret')[1]=={'id':'minghoo-id','label':'Minghoo','role':'admin'}
  code,job=request('/api/thc/control',token='minghoo-private-secret',body={'action':'backup','_auditActor':{'label':'Forged'}})
  assert code==200
  for _ in range(40):
   rows=request('/api/thc/control')[1]['jobs'];matching=next(row for row in rows if row['id']==job['job'])
   if matching['state']!='running':break
   time.sleep(.05)
  assert matching['actor']['label']=='Minghoo'
  records=[json.loads(line) for line in c.audit_path.read_text().splitlines()]
  assert any(row['action']=='control:backup' and row['outcome']=='complete' and row['actor'].get('label')=='Minghoo' for row in records if isinstance(row['actor'],dict))
  assert 'minghoo-private-secret' not in c.audit_path.read_text() and 'Forged' not in c.audit_path.read_text()

  # Shared map persists independently of game availability and browser state.
  assert request('/api/thc/invite',token='wrong')[0]==401
  assert request('/api/thc/invite')[1]['available'] is False
  c.log=root/'game.log';c.log.write_text('fixture game log\n');info=c.log.stat()
  prior_identity=c.process_identity;c.process_identity=lambda:'boot:123:456'
  state_path=api_ini.parent/'thc/error-watch.json'
  state_path.write_text(json.dumps({'invite':{'valid':True,'code':'ABCD-1234','processIdentity':'boot:123:456','logIdentity':f'{info.st_dev}:{info.st_ino}','observedAt':1000}}))
  assert request('/api/thc/invite')[1]['code']=='ABCD-1234'
  c.process_identity=lambda:'boot:999:999'
  assert request('/api/thc/invite')[1]['available'] is False
  c.process_identity=prior_identity
  import base64,copy
  png='iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aDWQAAAAASUVORK5CYII='
  profile={'schema':1,'config':{'minX':-100,'maxX':100,'minY':-200,'maxY':200},'anchors':[],'image':{'mime':'image/png','data':png}}
  assert request('/api/thc/map',token='wrong')[0]==401
  assert request('/api/thc/map/status')[1]=={'configured':False,'revision':''}
  assert request('/api/thc/map',token='viewer-test-secret',body={'profile':profile,'baseRevision':''})[0]==403
  code,published=request('/api/thc/map',body={'profile':profile,'baseRevision':''});assert code==200,published
  revision=published['revision'];assert revision
  shared=request('/api/thc/map',token='viewer-test-secret')[1];assert shared['profile']==profile
  assert 'profile' not in request('/api/thc/map/status')[1]
  restarted=control.DashboardProxy(c,{**config,'api_config':'api.ini','dashboard_file':str(dashboard)})
  assert restarted.shared_map.read()['profile']==profile
  assert (api_ini.parent/'thc/shared-map.json').stat().st_mode & 0o777==0o600
  # Stale drafts must never overwrite another admin's map.
  assert request('/api/thc/map',body={'profile':profile,'baseRevision':''})[0]==400
  invalid=copy.deepcopy(profile);invalid['config']={'anchors':[{'x':i,'y':i,'u':i/3,'v':i/3} for i in range(3)]}
  assert request('/api/thc/map',body={'profile':invalid,'baseRevision':revision})[0]==400
  invalid=copy.deepcopy(profile);invalid['image']['data']='not a PNG'
  assert request('/api/thc/map',body={'profile':invalid,'baseRevision':revision})[0]==400
  invalid=copy.deepcopy(profile);invalid['config']['maxX']=float('nan')
  assert request('/api/thc/map',body={'profile':invalid,'baseRevision':revision})[0]==400
  assert request('/api/thc/map')[1]['revision']==revision
  assert request('/api/thc/map',body={'profile':profile,'baseRevision':revision})[0]==200
  assert json.loads((api_ini.parent/'thc/shared-map.previous.json').read_text())['revision']==revision

 finally:web.shutdown();web.server_close();thread.join()
print('PASS: verified backup/restore, previous-save retention, path traversal refusal, offline-only backups, fixed process control, duplicate prevention, settings preservation, secret redaction real local Unix protocol and authenticated offline dashboard/backup jobs')
