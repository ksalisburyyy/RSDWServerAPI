"""Only disposable files and a local sleep fixture; never installs to the VPS."""
from pathlib import Path
import tempfile,shutil,json,subprocess,hashlib,sys
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    folder=Path(temp);server=folder/'server';server.mkdir();stage=folder/'release';stage.mkdir()
    game=server/'fake-game';shutil.copy2('/bin/sleep',game)
    target=server/'librsdwapi.so';target.write_bytes(b'previous library fixture')
    bins=folder/'bin';bins.mkdir();systemctl=bins/'systemctl';systemctl.write_text('#!/bin/sh\ncase \"$*\" in *show*) echo process;; esac\nexit 0\n');systemctl.chmod(0o700)
    import os
    env={**os.environ,'PATH':str(bins)+':'+os.environ['PATH']}
    watcher=folder/'thc_error_watch.py';watcher.write_text('previous watcher fixture')
    shutil.copy2(root/'tools/thc_error_watch.py',stage/'thc_error_watch.py')
    controller=folder/'thc_control.py';controller.write_text('previous controller fixture')
    shutil.copy2(root/'tools/thc_control.py',stage/'thc_control.py')
    dashboard=folder/'dashboard.html';dashboard.write_text('<html>previous</html>')
    prepared=folder/'prepared';prepared.mkdir();(prepared/'dashboard.html').write_bytes(dashboard.read_bytes());(prepared/'manifest.json').write_text(json.dumps({'version':'old','files':{'dashboard.html':hashlib.sha256(dashboard.read_bytes()).hexdigest()}}))
    config=folder/'config.json';config.write_text(json.dumps({'server_root':str(server),'game_executable':str(game),'dashboard_file':str(dashboard),'dashboard_update_directory':str(prepared)}))
    shutil.copy2(root/'tools/install_death_ledger.py',stage/'install_update.py')
    shutil.copy2(root/'dist/librsdwapi.so',stage/'librsdwapi.so')
    (stage/'dashboard.html').write_text('<html>Server Management</html>')
    (stage/'manifest.json').write_text(json.dumps({'version':'0.5.0-thc.8','sha256':hashlib.sha256((stage/'librsdwapi.so').read_bytes()).hexdigest(),'dashboardSha256':hashlib.sha256((stage/'dashboard.html').read_bytes()).hexdigest(),'controllerSha256':hashlib.sha256((stage/'thc_control.py').read_bytes()).hexdigest(),'watcherSha256':hashlib.sha256((stage/'thc_error_watch.py').read_bytes()).hexdigest()}))
    def install():return subprocess.run([sys.executable,str(stage/'install_update.py'),'--config',str(config)],text=True,capture_output=True,env=env)
    process=subprocess.Popen([str(game),'60'])
    try:
        result=install();assert result.returncode!=0 and 'still running' in result.stderr,(result.stdout,result.stderr)
        assert target.read_bytes()==b'previous library fixture' and not list(server.glob('librsdwapi.so.before-*'))
    finally:process.terminate();process.wait()
    result=install();assert result.returncode==0,(result.stdout,result.stderr)
    assert target.read_bytes()==(stage/'librsdwapi.so').read_bytes()
    assert dashboard.read_text()=='<html>Server Management</html>'
    assert controller.read_bytes()==(stage/'thc_control.py').read_bytes()
    assert watcher.read_bytes()==(stage/'thc_error_watch.py').read_bytes()
    assert (prepared/'dashboard.html').read_bytes()==dashboard.read_bytes()
    assert json.loads((prepared/'manifest.json').read_text())['version']=='0.5.0-thc.8'
    assert next(folder.glob('dashboard.html.before-*')).read_text()=='<html>previous</html>'
    assert next(server.glob('librsdwapi.so.before-*')).read_bytes()==b'previous library fixture'
    previous={file:file.read_bytes() for file in (target,dashboard,controller,watcher)}
    systemctl.write_text('#!/bin/sh\ncase \"$*\" in *show*) echo process;; *is-active*) exit 1;; esac\nexit 0\n')
    result=install();assert result.returncode!=0 and 'previous files restored' in result.stderr
    assert all(file.read_bytes()==data for file,data in previous.items())
    (stage/'librsdwapi.so').write_bytes(b'corrupted release')
    result=install();assert result.returncode!=0 and 'checksum failed' in result.stderr
print('PASS: native installer refuses running game, validates checksum/ELF, keeps verified old library, atomically installs while stopped')
