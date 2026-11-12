"""Built native API + real local proxy + fake curl + watcher. No game or Discord access."""
import importlib.util,json,os,socket,subprocess,sys,tempfile,threading,time,urllib.request,urllib.error,http.server
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
control=load('controller',ROOT/'tools/thc_control.py');watcher=load('watcher',ROOT/'tools/thc_error_watch.py')
def port():
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));return sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='thc-named-audit-') as directory:
 root=Path(directory);(root/'Saved').mkdir();(root/'Saved/world').write_text('fixture save')
 bins=root/'bin';bins.mkdir();captured=root/'captured.jsonl';fake=bins/'curl'
 fake.write_text('#!'+sys.executable+'\nimport os,sys,json\npayload=json.load(sys.stdin)\nfd=os.open(os.environ["THC_CAPTURE"],os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)\nos.write(fd,(json.dumps(payload)+"\\n").encode());os.close(fd)\nprint("204",end="")\n');fake.chmod(0o700)
 api_port=port();owner='fixture-owner-secret';settings=root/'settings.ini'
 settings.write_text(f'[API]\nEnabled=true\nBindAddress=127.0.0.1\nPort={api_port}\nBearerToken={owner}\n[RCON]\nEnabled=false\n[Discord]\nEnabled=true\nAdminWebhookUrl=https://discord.com/api/webhooks/123456789012345678/fixture-only\n')
 dashboard=root/'dashboard.html';dashboard.write_text('<html>fixture</html>');game=root/'fake-game';game.write_text('fixture only')
 env={**os.environ,'LD_PRELOAD':str(ROOT/'dist/librsdwapi.so'),'RSDWAPI_DIR':str(root),'PATH':str(bins)+':'+os.environ['PATH'],'THC_CAPTURE':str(captured)}
 log=(root/'host.log').open('w');host=subprocess.Popen([str(ROOT/'build/tests/preload_host')],stdin=subprocess.PIPE,stdout=log,stderr=log,env=env)
 cfg={'server_root':str(root),'save_directory':'Saved','backup_directory':str(root/'backups'),'game_executable':'fake-game','mode':'disabled','api_config':'settings.ini','dashboard_file':str(dashboard)}
 c=control.Controller(cfg);proxy=control.DashboardProxy(c,cfg)
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),control.DashboardHandler);server.proxy=proxy
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 unix=None;unix_thread=None
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
 def request(path,token=owner,body=None):
  req=urllib.request.Request(f'http://127.0.0.1:{server.server_port}'+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
  try:
   with opener.open(req,timeout=4) as response:return response.status,json.load(response)
  except urllib.error.HTTPError as error:return error.code,json.load(error)
 try:
  for _ in range(100):
   if host.poll() is not None:raise AssertionError((root/'host.log').read_text())
   try:
    if request('/api/health')[1].get('engineReady') is not None and (root/'thc/state.json').exists():break
   except (OSError,KeyError):pass
   time.sleep(.05)
  unix=control.Server(str(root/'thc/control.sock'),control.Handler);unix.controller=c
  unix_thread=threading.Thread(target=unix.serve_forever,daemon=True);unix_thread.start()
  a=request('/api/thc/access/create',body={'label':'Minghoo','role':'admin'})[1]
  b=request('/api/thc/access/create',body={'label':'Nova','role':'admin'})[1]
  assert request('/api/thc/access/me',a['token'])[1]['label']=='Minghoo'
  assert request('/api/thc/access/me',b['token'])[1]['label']=='Nova'
  # Engine unavailable in this fixture; both failure audits must retain the correct identity.
  for token,label in ((a['token'],'Minghoo'),(b['token'],'Nova')):
   status,_=request('/api/give',token,{'player':'Hero','item':'ITEM_Test','count':1,'actor':{'label':'Forged'}});assert status==503
  # Async completion must keep its initiating identity even while another credential is used.
  status,job=request('/api/thc/control',a['token'],{'action':'backup','_auditActor':{'label':'Forged'}});assert status==200
  request('/api/thc/access/me',b['token'])
  for _ in range(60):
   rows=request('/api/thc/control',b['token'])[1]['jobs'];completed=next(row for row in rows if row['id']==job['job'])
   if completed['state']!='running':break
   time.sleep(.05)
  assert completed['actor']['label']=='Minghoo' and completed['state']=='complete'
  direct=urllib.request.Request(f'http://127.0.0.1:{api_port}/api/thc/control',data=json.dumps({'action':'backup','_auditActor':{'label':'Forged'}}).encode(),headers={'Authorization':'Bearer '+b['token'],'Content-Type':'application/json'})
  with opener.open(direct,timeout=4) as response:direct_job=json.load(response)['job']
  for _ in range(60):
   rows=request('/api/thc/control')[1]['jobs'];direct_done=next(row for row in rows if row['id']==direct_job)
   if direct_done['state']!='running':break
   time.sleep(.05)
  assert direct_done['actor']['label']=='Nova' and direct_done['state']=='complete'
  records=[json.loads(line) for line in c.audit_path.read_text().splitlines()]
  gives=[row for row in records if row['action']=='/api/give'];assert len(gives)==2
  assert [row['actor']['label'] for row in gives]==['Minghoo','Nova']
  assert all(row['details']['nativeAuditQueued'] is True for row in gives)
  watch=watcher.Watcher(root/'watch-state.json');watch.state['files']['Admin audit']={'identity':'missing','offset':0}
  watch.scan('Admin audit',c.audit_path,int(time.time()))
  posts=[row['content'] for row in watch.state['pending']]
  assert len(posts)==2 and 'Minghoo (admin)' in posts[0] and 'Create backup' in posts[0] and 'Nova (admin)' in posts[1]
  for _ in range(120):
   payloads=[json.loads(line)['content'] for line in captured.read_text().splitlines()] if captured.exists() else []
   api_posts=[text for text in payloads if '**Action:** Give item' in text]
   if len(api_posts)>=2:break
   time.sleep(.05)
  assert len(api_posts)==2,payloads
  assert 'Minghoo (admin)' in api_posts[0] and a['id'] in api_posts[0]
  assert 'Nova (admin)' in api_posts[1] and b['id'] in api_posts[1]
  combined=json.dumps(records)+json.dumps(payloads)+json.dumps(posts)
  for secret in (owner,a['token'],b['token']):assert secret not in combined
  assert 'Forged' not in combined
  print('PASS: exact built API/proxy label+ID attribution for two credentials, one Discord path per action, private ledger retention, immutable async actor and no token disclosure (fake curl, no Discord)')
 finally:
  if unix:unix.shutdown();unix.server_close();unix_thread.join()
  server.shutdown();server.server_close();thread.join();host.terminate()
  try:host.wait(timeout=8)
  except subprocess.TimeoutExpired:host.kill();host.wait()
  log.close()
