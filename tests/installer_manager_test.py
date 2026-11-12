"""The protected user manager is not a game; arbitrary unreadable processes still fail closed."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parents[1]/'tools/install_death_ledger.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Group:
    def read_text(self):return '0::/user.slice/user-1001.slice/user@1001.service/init.scope\n'
class Entry:
    name='19323'
    def __truediv__(self,name):return Group()
with patch.object(m.os,'geteuid',return_value=1001),patch.object(m.subprocess,'run',return_value=SimpleNamespace(stdout='19323\n')):
    assert m.protected_user_manager(Entry())
    wrong=Entry();wrong.name='12345';assert not m.protected_user_manager(wrong)
    with patch.object(Group,'read_text',return_value='0::/user.slice/user-1001.slice/session-c1.scope\n'):
        assert not m.protected_user_manager(Entry())
    with patch.object(m.subprocess,'run',side_effect=FileNotFoundError):assert not m.protected_user_manager(Entry())
print('PASS: protected systemd user manager recognized by system MainPID and init.scope; other PIDs/cgroups and lookup failures remain refused')

class Text:
    def __init__(self,value):self.value=value
    def read_text(self):return self.value
class Child:
    name='19324'
    parent='19323'
    group='0::/user.slice/user-1001.slice/user@1001.service/init.scope\n'
    def __truediv__(self,name):return Text('PPid:\t'+self.parent+'\n' if name=='status' else self.group)
with patch.object(m.os,'geteuid',return_value=1001),patch.object(m.subprocess,'run',return_value=SimpleNamespace(stdout='19323\n')):
    assert m.protected_user_manager(Child())
    child=Child();child.parent='99999';assert not m.protected_user_manager(child)
    child=Child();child.group='0::/user.slice/user-1001.slice/session-c1.scope\n';assert not m.protected_user_manager(child)
print('PASS: protected direct helper accepted only with verified manager PPid AND init.scope; unrelated parent/session remains refused')
