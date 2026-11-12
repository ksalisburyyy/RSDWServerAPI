import importlib.util
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parents[1]/'tools/install_death_ledger.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Field:
    def __init__(self,text):self.text=text
    def read_text(self):return self.text
    def read_bytes(self):return self.text.encode()
class Entry:
    name='3408689'
    comm='dbus-daemon\n'
    argv='/usr/bin/dbus-daemon\0--session'
    def __truediv__(self,name):return Field(self.comm if name=='comm' else self.argv)
exe=Path('/fixture/RSDragonwildsServer-Linux-Shipping')
with patch.object(Path,'iterdir',return_value=iter([Entry()])),patch.object(m.os,'readlink',side_effect=PermissionError):m.stopped(exe)
entry=Entry();entry.comm=exe.name[:15]+'\n'
with patch.object(Path,'iterdir',return_value=iter([entry])),patch.object(m.os,'readlink',side_effect=PermissionError):
    try:m.stopped(exe);raise AssertionError('protected game allowed')
    except SystemExit as error:assert 'game process' in str(error)
entry=Entry();entry.argv=str(exe)+'\0-log'
with patch.object(Path,'iterdir',return_value=iter([entry])),patch.object(m.os,'readlink',side_effect=PermissionError):
    try:m.stopped(exe);raise AssertionError('protected game argv allowed')
    except SystemExit as error:assert 'game process' in str(error)
print('PASS: unrelated protected process skipped; matching protected game comm/argv refused')
