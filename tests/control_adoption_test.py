"""Existing-process control tests use a private copy of /bin/sleep only."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from unittest.mock import patch
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('thc_control',root/'tools/thc_control.py');control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
spec=importlib.util.spec_from_file_location('thc_preflight',root/'tools/thc_preflight.py');preflight=importlib.util.module_from_spec(spec);spec.loader.exec_module(preflight)
with tempfile.TemporaryDirectory() as directory:
 base=Path(directory);server=base/'server';server.mkdir();(server/'saves').mkdir();exe=server/'synthetic-game';shutil.copy2(shutil.which('sleep'),exe)
 config={'server_root':str(server),'save_directory':'saves','backup_directory':str(base/'backups'),'mode':'owned_process','command':[str(exe),'60'],'game_executable':str(exe)}
 normal=control.Controller(config);adopt=control.Controller({**config,'adopt_existing':True});children=[]
 def spawn():
  child=subprocess.Popen([str(exe),'60'],start_new_session=True);children.append(child)
  for _ in range(100):
   if child.pid in adopt.external_pids():return child
   assert child.poll() is None;time.sleep(.01)
  raise AssertionError('fixture process was not observed')
 try:
  child=spawn()
  try:normal._stop();raise AssertionError('unmanaged process was stopped without opt-in')
  except ValueError:pass
  assert child.poll() is None
  try:adopt._start();raise AssertionError('duplicate process was launched')
  except ValueError:pass
  uid=os.geteuid()
  with patch.object(control.os,'geteuid',return_value=uid+1):
   try:adopt._stop();raise AssertionError('wrong-owner process was signalled')
   except ValueError:pass
  assert child.poll() is None
  assert 'exit observed' in adopt._stop();child.wait(timeout=3);assert not adopt.external_pids()
  a,b=spawn(),spawn()
  try:adopt._stop();raise AssertionError('ambiguous processes were stopped')
  except ValueError:pass
  assert a.poll() is None and b.poll() is None
 finally:
  for child in children:
   if child.poll() is None:child.terminate()
   child.wait(timeout=3)
 for unit in ['thc-dashboard.service','thc-discord.service','thc-control.service']:
  try:control.Controller({**config,'mode':'systemd_user','service_unit':unit});raise AssertionError('non-game unit accepted')
  except ValueError:pass
 # Preflight must not create a backup directory, socket, process or secret output.
 (server/'api.ini').write_text('[API]\nPort=8081\nBearerToken=private-test-credential\n');(server/'game.ini').write_text('[Server]\nServerName=fixture\n');(server/'game.log').write_text('fixture');(server/'dashboard.html').write_text('fixture')
 planned={**config,'backup_directory':str(base/'not-created'),'api_config':'api.ini','settings_file':'game.ini','log_file':'game.log','socket':str(server/'thc/control.sock'),'dashboard_file':str(server/'dashboard.html')}
 before={str(p.relative_to(base)) for p in base.rglob('*')};result=preflight.inspect(planned);after={str(p.relative_to(base)) for p in base.rglob('*')}
 assert result['ready'] and before==after and not (base/'not-created').exists();assert 'private-test-credential' not in json.dumps(result)
print('PASS: opt-in pidfd process control, UID/executable checks, duplicate/ambiguous refusal, protected service refusal and read-only secret-free preflight')

with tempfile.TemporaryDirectory() as directory:
 base=Path(directory);sock=base/'control.sock'
 import socket
 sock.write_text('preserve')
 try:
  with control.socket_lock(sock):raise AssertionError('regular file was replaced')
 except ValueError:pass
 assert sock.read_text()=='preserve';sock.unlink()
 with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as old:old.bind(str(sock))
 with control.socket_lock(sock):
  assert not sock.exists()
  try:
   with control.socket_lock(sock):raise AssertionError('duplicate startup lock accepted')
  except ValueError:pass
 with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as active:
  active.bind(str(sock));active.listen()
  try:
   with control.socket_lock(sock):raise AssertionError('live listener was replaced')
  except ValueError:pass
  assert sock.exists()
print('PASS: stale-socket recovery, single-agent locking and active-listener/non-socket preservation')
# The game child must keep writing after its launching controller exits.
with tempfile.TemporaryDirectory() as directory:
 base=Path(directory);server=base/'server';server.mkdir();(server/'saves').mkdir();exe=server/'synthetic-shell';shutil.copy2(shutil.which('sh'),exe)
 config={'server_root':str(server),'save_directory':'saves','backup_directory':str(base/'backups'),'mode':'owned_process','adopt_existing':True,'command':[str(exe),'-c','while :; do echo fixture-output; sleep 0.05; done'],'game_executable':str(exe)}
 config_file=base/'fixture.json';config_file.write_text(json.dumps(config));pid_file=base/'child.pid'
 code="""import importlib.util,json,os,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('control',sys.argv[1]);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
c=module.Controller(json.loads(Path(sys.argv[2]).read_text()));c._start();Path(sys.argv[3]).write_text(str(c.process.pid));os._exit(0)
"""
 import sys
 subprocess.run([sys.executable,'-c',code,str(root/'tools/thc_control.py'),str(config_file),str(pid_file)],check=True,timeout=5)
 manager=control.Controller(config)
 try:
  assert manager.running();output=server/'thc-control-process.log'
  time.sleep(.2);first=output.stat().st_size;time.sleep(.2);assert output.stat().st_size>first and manager.running()
  assert output.stat().st_mode&0o077==0
 finally:
  if manager.running():manager._stop()
 assert not manager.running()
print('PASS: game output survives controller exit without a broken pipe; private regular log and later opt-in process management')
