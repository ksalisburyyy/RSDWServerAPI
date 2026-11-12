"""Local updater fixtures only; never invokes SteamCMD or a real game."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import hashlib
spec=importlib.util.spec_from_file_location('control',Path(__file__).resolve().parents[1]/'tools/thc_control.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as tmp:
    base=Path(tmp);root=base/'server';root.mkdir();saves=root/'Saved';saves.mkdir();(saves/'world').write_text('world data')
    game=root/'game';game.write_text('fixture')
    ui=base/'ui';ui.mkdir();(ui/'dashboard.html').write_text('<html>previous</html>')
    release=base/'release';release.mkdir();html=b'<html>updated</html>'
    (release/'dashboard.html').write_bytes(html)
    manifest={'version':'test.1','files':{'dashboard.html':hashlib.sha256(html).hexdigest()}}
    (release/'manifest.json').write_text(json.dumps(manifest))
    steam=base/'steam-fixture';steam.write_text('#!/usr/bin/env python3\nimport sys\nassert sys.argv[1:]==["+force_install_dir",'+repr(str(root))+',"+login","anonymous","+app_update","4019830","validate","+quit"]\nprint("Success! App \'4019830\' fully installed.")\n');steam.chmod(0o700)
    c=m.Controller({'server_root':str(root),'save_directory':'Saved','backup_directory':str(base/'backups'),'game_executable':'game','mode':'disabled','steamcmd_path':str(steam),'dashboard_file':str(ui/'dashboard.html'),'dashboard_update_directory':str(release)})
    assert c.update_status()['serverConfigured']
    assert c.update_status()['dashboardVersion']=='test.1'
    result=c.update_server();assert 'Game remains stopped' in result and not c.update_marker.exists()
    assert len(c.list_backups())==1 and (saves/'world').read_text()=='world data'
    c.dashboard_apply();assert (ui/'dashboard.html').read_bytes()==html
    c.dashboard_apply(True);assert (ui/'dashboard.html').read_text()=='<html>previous</html>'
    # Tampering must be rejected without changing the served UI.
    (release/'dashboard.html').write_text('<html>tampered</html>')
    try:c.dashboard_apply();raise AssertionError('tampering accepted')
    except ValueError:pass
    assert (ui/'dashboard.html').read_text()=='<html>previous</html>'
    # Restrict release files to static UI assets; never install executable/controller code.
    manifest['files']['thc_control.py']='0'*64;(release/'manifest.json').write_text(json.dumps(manifest))
    try:c.dashboard_bundle();raise AssertionError('executable update accepted')
    except ValueError:pass
    # Success text without exit success, and exit success without success text, both fail closed.
    for program in ('print("Success! App \'4019830\' fully installed.");raise SystemExit(1)','print("ERROR! Download failed")'):
        steam.write_text('#!/usr/bin/env python3\n'+program+'\n')
        try:c.update_server();raise AssertionError('failed SteamCMD accepted')
        except ValueError:pass
        assert c.update_marker.exists()
        try:c._start();raise AssertionError('start accepted after failed update')
        except ValueError as error:assert 'Previous server update' in str(error)
    c.running=lambda:True
    try:c.update_server();raise AssertionError('live update accepted')
    except ValueError as error:assert 'Stop the game' in str(error)
print('Updater checks passed: backup, fixed argv, failures, start interlock, checksums, static-only release, rollback, running-game refusal')
