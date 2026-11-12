import importlib.util
import tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('watch',Path(__file__).resolve().parents[1]/'tools/thc_watch.py')
watch=importlib.util.module_from_spec(spec);spec.loader.exec_module(watch)
with tempfile.TemporaryDirectory() as folder:
 path=Path(folder)/'watch.json';m=watch.Monitor(path)
 m.observe(False,0);m.observe(False,5);m.observe(False,10);assert not m.state['pending']
 m.observe(True,15);assert len(m.state['pending'])==1
 m.deliver(15,lambda payload:(429,20));assert m.state['pending'][0]['due']==35
 called=[];m.deliver(34,lambda payload:called.append(payload));assert not called
 m=watch.Monitor(path);m.deliver(36,lambda payload:(204,0));assert not m.state['pending']
 m.observe(None,40);assert m.state['online'] is True
 m.observe(False,45);m.observe(False,50);assert not m.state['pending'];m.observe(False,55);assert len(m.state['pending'])==1
 m.observe(False,60);assert len(m.state['pending'])==1
 m.observe(True,65);assert len(m.state['pending'])==2
print('PASS: watcher availability debounce, startup/recovery, authentication distinction, durable Retry-After and restart replay (mock transport)')
