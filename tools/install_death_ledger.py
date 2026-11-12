#!/usr/bin/env python3
"""Verify stopped game; install matching library, dashboard and optional controller with backups."""
import argparse,hashlib,json,os,shutil,struct,subprocess,tempfile,time,py_compile
from pathlib import Path

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def protected_user_manager(entry):
    # systemd may deliberately make its per-user manager non-dumpable. Confirm
    # its identity with the system manager, rather than trusting a process name.
    try:
        uid=os.geteuid()
        result=subprocess.run(['systemctl','show','user@'+str(uid)+'.service','--property=MainPID','--value'],
                              check=True,capture_output=True,text=True,timeout=5)
        manager=result.stdout.strip()
        if not manager.isdigit() or int(manager)<=1:return False
        if manager!=entry.name:
            # PAM helpers can also be protected. Accept only a direct child of
            # the independently verified manager, inside its exact init.scope.
            status=(entry/'status').read_text().splitlines()
            parent=next((line.split(':',1)[1].strip() for line in status if line.startswith('PPid:')),'')
            if parent!=manager:return False
        group='/user@'+str(uid)+'.service/init.scope'
        return any(line.rstrip().endswith(group) for line in (entry/'cgroup').read_text().splitlines())
    except (OSError,subprocess.SubprocessError,ValueError):return False

def stopped(exe):
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():continue
        try:
            target=os.readlink(entry/'exe')
            if target.endswith(' (deleted)'):target=target[:-10]
            if Path(target).resolve()==exe:raise SystemExit('STOP: game is still running. Stop it through the dashboard; nothing replaced.')
        except FileNotFoundError:continue
        except PermissionError:
            # Unrelated protected services are normal. Inspect the game name
            # instead of requiring executable access to every owned process.
            try:comm=(entry/'comm').read_text().rstrip('\n')
            except FileNotFoundError:continue
            except PermissionError:continue
            if comm==exe.name[:15]:raise SystemExit('STOP: a game process is still present ('+entry.name+'); nothing replaced.')
            try:argv=(entry/'cmdline').read_bytes().split(b'\0',1)[0]
            except (FileNotFoundError,PermissionError):continue
            if argv and Path(os.fsdecode(argv)).name==exe.name:raise SystemExit('STOP: a game process is still present ('+entry.name+'); nothing replaced.')

def atomic_install(source,target):
    fd,tmp=tempfile.mkstemp(dir=target.parent,prefix='.thc-install-')
    try:
        with os.fdopen(fd,'wb') as out,source.open('rb') as inp:shutil.copyfileobj(inp,out);out.flush();os.fsync(out.fileno())
        if sha(Path(tmp))!=sha(source):raise ValueError('Staged checksum failed')
        os.replace(tmp,target)
        directory=os.open(target.parent,os.O_RDONLY|os.O_DIRECTORY)
        try:os.fsync(directory)
        finally:os.close(directory)
    finally:
        if os.path.exists(tmp):os.unlink(tmp)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--config',default=str(Path.home()/'.config/thc-control.json'));args=parser.parse_args()
    stage=Path(__file__).resolve().parent;manifest=json.loads((stage/'manifest.json').read_text())
    library=stage/'librsdwapi.so'
    if sha(library)!=manifest['sha256']:raise SystemExit('Library checksum failed; nothing replaced.')
    with library.open('rb') as f:header=f.read(64)
    if header[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<HH',header,16)!=(3,62):raise SystemExit('Expected an ELF64 x86-64 shared library; nothing replaced.')
    config=json.loads(Path(args.config).read_text());root=Path(config['server_root']).resolve(strict=True)
    exe=Path(config['game_executable']);exe=(root/exe).resolve() if not exe.is_absolute() else exe.resolve()
    if root not in exe.parents:raise SystemExit('Game executable must be inside server root; nothing replaced.')
    if root.stat().st_uid!=os.geteuid():raise SystemExit('Run as the server owner, not root; nothing replaced.')
    target=root/'librsdwapi.so'
    if target.is_symlink() or not target.is_file() or target.stat().st_uid!=os.geteuid():raise SystemExit('Expected an owned regular librsdwapi.so; nothing replaced.')
    dashboard=Path(config['dashboard_file']).resolve(strict=True) if config.get('dashboard_file') else None
    html=stage/'dashboard.html'
    if not dashboard or dashboard.name!='dashboard.html' or dashboard.stat().st_uid!=os.geteuid():raise SystemExit('Expected an owned dashboard.html; nothing replaced.')
    if sha(html)!=manifest['dashboardSha256']:raise SystemExit('Dashboard checksum failed; nothing replaced.')
    controller=None;controller_backup=None
    if manifest.get('controllerSha256'):
        controller=dashboard.parent/'thc_control.py'
        if controller.is_symlink() or not controller.is_file() or controller.stat().st_uid!=os.geteuid():raise SystemExit('Expected an owned controller; nothing replaced.')
        if sha(stage/'thc_control.py')!=manifest['controllerSha256']:raise SystemExit('Controller checksum failed; nothing replaced.')
        py_compile.compile(str(stage/'thc_control.py'),doraise=True)
        mode=subprocess.check_output(['systemctl','--user','show','thc-control.service','-p','KillMode','--value'],text=True,timeout=10).strip()
        if mode!='process':raise SystemExit('Controller KillMode must be process; nothing replaced.')
    watcher=None;watcher_backup=None
    if manifest.get('watcherSha256'):
        watcher=dashboard.parent/'thc_error_watch.py'
        if watcher.is_symlink() or not watcher.is_file() or watcher.stat().st_uid!=os.geteuid():raise SystemExit('Expected an owned notification watcher; nothing replaced.')
        if sha(stage/'thc_error_watch.py')!=manifest['watcherSha256']:raise SystemExit('Watcher checksum failed; nothing replaced.')
        py_compile.compile(str(stage/'thc_error_watch.py'),doraise=True)
    stopped(exe)
    ui_backup=dashboard.with_name('dashboard.html.before-thc8-'+str(time.time_ns()))
    shutil.copy2(dashboard,ui_backup)
    if sha(ui_backup)!=sha(dashboard):raise SystemExit('Dashboard backup verification failed; nothing replaced.')
    backup=root/('librsdwapi.so.before-thc8-'+str(time.time_ns()))
    shutil.copy2(target,backup)
    if sha(backup)!=sha(target):raise SystemExit('Backup verification failed; nothing replaced.')
    plans=[(library,target,backup),(html,dashboard,ui_backup)]
    if controller:
        controller_backup=controller.with_name('thc_control.py.before-thc8-'+str(time.time_ns()))
        shutil.copy2(controller,controller_backup)
        if sha(controller_backup)!=sha(controller):raise SystemExit('Controller backup verification failed; nothing replaced.')
        plans.append((stage/'thc_control.py',controller,controller_backup))
    if watcher:
        watcher_backup=watcher.with_name('thc_error_watch.py.before-thc8-'+str(time.time_ns()))
        shutil.copy2(watcher,watcher_backup)
        if sha(watcher_backup)!=sha(watcher):raise SystemExit('Watcher backup verification failed; nothing replaced.')
        plans.append((stage/'thc_error_watch.py',watcher,watcher_backup))
    stopped(exe)
    applied=[]
    try:
        for source,destination,previous in plans:
            applied.append((destination,previous));atomic_install(source,destination)
        if controller:
            subprocess.run(['systemctl','--user','restart','thc-control.service'],check=True,timeout=30)
            subprocess.run(['systemctl','--user','is-active','--quiet','thc-control.service'],check=True,timeout=10)
        if watcher:
            subprocess.run(['systemctl','--user','restart','thc-error-watch.service'],check=True,timeout=30)
            subprocess.run(['systemctl','--user','is-active','--quiet','thc-error-watch.service'],check=True,timeout=10)
    except Exception as error:
        for destination,previous in reversed(applied):atomic_install(previous,destination)
        if controller:subprocess.run(['systemctl','--user','restart','thc-control.service'],timeout=30)
        if watcher:subprocess.run(['systemctl','--user','restart','thc-error-watch.service'],timeout=30)
        raise SystemExit('Installation failed; previous files restored: '+str(error))
    # Advance the initially prepared UI release only when it is exactly the old
    # installed dashboard. Preserve any different release the owner has staged.
    release_path=config.get('dashboard_update_directory')
    if release_path:
        try:
            release=Path(release_path).resolve(strict=True);prepared=release/'manifest.json'
            info=json.loads(prepared.read_text())
            if info.get('files')=={'dashboard.html':sha(ui_backup)} and sha(release/'dashboard.html')==sha(ui_backup):
                for path,data in ((release/'dashboard.html',html.read_bytes()),(prepared,(json.dumps({'version':manifest['version'],'files':{'dashboard.html':manifest['dashboardSha256']}},indent=2)+'\n').encode())):
                    fd,name=tempfile.mkstemp(dir=release,prefix='.thc-release-')
                    try:
                        with os.fdopen(fd,'wb') as out:out.write(data);out.flush();os.fsync(out.fileno())
                        os.replace(name,path)
                    finally:
                        if os.path.exists(name):os.unlink(name)
        except (OSError,ValueError,KeyError,TypeError):
            print('Prepared dashboard release was not refreshed; check its checksum/status before applying it.')
    print('Installed '+manifest['version']+'. Game remains stopped.')
    print('Previous library: '+str(backup))
    print('Server Management dashboard installed. Previous dashboard: '+str(ui_backup))
    print('Controller updated: '+str(controller_backup) if controller else 'Controller was not changed.')
    print('Watcher updated: '+str(watcher_backup) if watcher else 'Watcher was not changed.')
    print('Existing access tokens, credentials, map and historical ledger were not changed.')
    print('Start through the dashboard and refresh. Compatibility & Recovery is in Server Management.')
if __name__=='__main__':main()
