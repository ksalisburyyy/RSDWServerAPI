#!/usr/bin/env python3
"""Optional localhost Unix-socket control service. Never installed by the mod.
Only paths/commands from the protected config are used; requests cannot choose
shell commands, filesystem roots, service units or executable arguments.
"""
import argparse
import base64
import math
import struct
import configparser
import contextlib
import fcntl
import hashlib
import http.server
import hmac
import urllib.error
import urllib.request
import urllib.parse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import select
import socket
import socketserver
import stat
import subprocess
import tempfile
import threading
import time
import uuid
import zipfile


def atomic_json(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(dir=path.parent,prefix='.thc-')
    try:
        with os.fdopen(fd,'w') as f:json.dump(data,f);f.flush();os.fsync(f.fileno())
        os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)

def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def redacted(text):
    text=re.sub(r'https://(?:\w+\.)?discord(?:app)?\.com/api/webhooks/\S+','[webhook redacted]',text,flags=re.I)
    text=re.sub(r'(?i)(Bearer\s+)\S+',r'\1[redacted]',text)
    return re.sub(r'(?i)((?:WorldPassword|ServerPassword|BearerToken|Password)\s*=).*',r'\1[redacted]',text)

def audit_details(path, value):
    if not isinstance(value,dict):return {}
    fields={
        '/api/kick':('player','reason'),'/api/ban':('player','reason'),'/api/unban':('player',),
        '/api/give':('player','item','count'),'/api/broadcast':('sender','message'),
        '/api/thc/teleport':('player','x','y','z'),
    }
    if path=='/api/thc/console':
        command=value.get('command','')
        if not isinstance(command,str):return {}
        sensitive=re.search(r'password|token|secret|webhook|credential|\bauth',command,re.I)
        return {'command':'[sensitive command arguments redacted]' if sensitive else redacted(command[:500])}
    if path in ('/api/thc/settings','/api/thc/access/create','/api/thc/access/revoke'):
        known={'apiPort','combatTracking','saveCommand','discordEnabled','discordUsername','chatWebhook','deathWebhook','presenceWebhook','statusWebhook','adminWebhook','chatThreadId','deathThreadId','presenceThreadId','statusThreadId','adminThreadId','label','role','id'}
        return {'changedFields':sorted(k for k in value if k in known)}
    if path=='/api/thc/map':return {'operation':'publish image/calibration; image data omitted'}
    if path=='/api/thc/control':
        action=value.get('action')
        return {'action':action if action in ('start','stop','restart','backup','restore','update_game_settings','update_server','update_dashboard','rollback_dashboard') else 'unsupported action'}
    result={}
    for key in fields.get(path,()):
        item=value.get(key)
        if type(item) in (int,float):result[key]=item if math.isfinite(item) else '[nonfinite value]'
        elif isinstance(item,str):result[key]=redacted(item[:500])
    return result

class Controller:
    def __init__(self,config):
        self.root=Path(config['server_root']).resolve(strict=True)
        self.saves=self.inside(config['save_directory'])
        self.backups=Path(config['backup_directory']).resolve()
        if self.saves in self.backups.parents or self.backups==self.saves or self.backups in self.saves.parents:
            raise ValueError('Backup and save directories must be separate')
        self.backups.mkdir(parents=True,exist_ok=True);os.chmod(self.backups,0o700)
        self.log=self.inside(config['log_file']) if config.get('log_file') else None
        self.output=self.inside(config.get('process_output','thc-control-process.log'))
        if self.output==self.log or self.output==self.saves or self.saves in self.output.parents:raise ValueError('Process output must be separate from game logs and saves')
        self.ini=self.inside(config['settings_file']) if config.get('settings_file') else None
        self.mode=config.get('mode','disabled');self.command=config.get('command',[]);self.unit=config.get('service_unit','')
        if self.mode not in ('disabled','owned_process','systemd_user'):raise ValueError('Invalid mode')
        if self.mode=='systemd_user' and not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.@-]*\.service',self.unit):raise ValueError('Invalid fixed service unit')
        if self.mode=='owned_process' and (not self.command or not all(isinstance(v,str) and '\x00' not in v for v in self.command)):raise ValueError('Configure a fixed foreground command')
        self.adopt=config.get('adopt_existing',False)
        if not isinstance(self.adopt,bool):raise ValueError('adopt_existing must be true or false')
        if self.adopt and (self.mode!='owned_process' or not hasattr(os,'pidfd_open') or not hasattr(signal,'pidfd_send_signal')):raise ValueError('Existing-process management requires owned_process mode and Linux pidfd support')
        if self.mode=='systemd_user' and self.unit in {'thc-dashboard.service','thc-discord.service','thc-control.service'}:raise ValueError('Dashboard, Discord and control services cannot be used as the game unit')
        self.exe=self.inside(config['game_executable']) if config.get('game_executable') else None
        if self.mode!='disabled' and (self.exe is None or not self.exe.is_file()):raise ValueError('Configure the real game executable before enabling process control')
        self.steamcmd=config.get('steamcmd_path','')
        if self.steamcmd and (not isinstance(self.steamcmd,str) or not Path(self.steamcmd).is_absolute()):raise ValueError('steamcmd_path must be an absolute executable path')
        self.dashboard_file=Path(config['dashboard_file']).resolve() if config.get('dashboard_file') else None
        if self.dashboard_file and self.dashboard_file.name!='dashboard.html':raise ValueError('Dashboard updates require dashboard.html as the dashboard filename')
        self.dashboard_release=Path(config['dashboard_update_directory']).resolve() if config.get('dashboard_update_directory') else None
        self.update_timeout=int(config.get('server_update_timeout',3600))
        if not 60<=self.update_timeout<=14400:raise ValueError('Server update timeout must be 60..14400 seconds')
        self.update_marker=self.backups/'server-update-incomplete.json'
        self.update_log=self.backups/'server-update.log'
        self.max_bytes=int(config.get('max_backup_bytes',2*1024**3));self.process=None
        self.lock=threading.Lock();self.jobs_lock=threading.Lock();self.jobs={}
        self.audit_lock=threading.Lock();self.audit_error=False
        self.audit_api=self.inside(config['api_config']) if config.get('api_config') else None
        self.audit_path=(self.audit_api.parent if self.audit_api else self.root)/'thc/controller-audit.log'
    def audit(self, action, outcome, details=None, actor='shared administration'):
        try:
            secrets=[]
            for file in (self.audit_api,self.ini):
                if file and file.is_file() and file.stat().st_size<=128*1024:
                    for line in file.read_text().splitlines():
                        pair=re.match(r'\s*([^=;#]+)=(.*)',line)
                        if pair and re.search('password|token|webhook',pair.group(1),re.I) and pair.group(2):secrets.append(pair.group(2).strip().strip('"\''))
            def clean(value):
                if isinstance(value,str):
                    value=redacted(value)
                    for secret in sorted((s for s in secrets if s),key=len,reverse=True):value=value.replace(secret,'[redacted]')
                    return value
                if isinstance(value,dict):return {key:clean(item) for key,item in value.items()}
                if isinstance(value,list):return [clean(item) for item in value]
                return value
            record={'schema':1,'id':uuid.uuid4().hex,'time':int(time.time()),'actor':clean(actor),'action':clean(action[:128]),'outcome':outcome,'details':clean(details or {})}
            with self.audit_lock:
                self.audit_path.parent.mkdir(parents=True,exist_ok=True)
                os.chmod(self.audit_path.parent,0o700)
                if self.audit_path.is_symlink():raise ValueError('Linked audit log refused')
                if self.audit_path.exists() and self.audit_path.stat().st_size>20*1024*1024:
                    self.audit_path.rename(self.audit_path.with_name('controller-audit.previous.log'))
                fd=os.open(self.audit_path,os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_NOFOLLOW,0o600)
                with os.fdopen(fd,'w') as stream:
                    os.fchmod(stream.fileno(),0o600);stream.write(json.dumps(record,allow_nan=False)+'\n');stream.flush();os.fsync(stream.fileno())
            self.audit_error=False
        except (OSError,ValueError,TypeError):self.audit_error=True
    def inside(self,path):
        value=Path(path);value=(self.root/value).resolve() if not value.is_absolute() else value.resolve()
        if value==self.root or self.root not in value.parents:raise ValueError('Path must be inside the configured server root')
        return value
    def external_pids(self):
        result=[]
        if not self.exe:return result
        for entry in Path('/proc').iterdir():
            if not entry.name.isdigit():continue
            try:
                if (entry/'exe').resolve(strict=True)==self.exe:result.append(int(entry.name))
            except (OSError,RuntimeError):pass
        return result
    def unit_active(self):
        result=subprocess.run(['systemctl','--user','is-active','--',self.unit],capture_output=True,text=True,timeout=5)
        if result.returncode not in (0,3):raise ValueError('Could not query configured user service')
        return result.returncode==0
    def running(self):
        if self.process is not None and self.process.poll() is None:return True
        if self.mode=='systemd_user':return self.unit_active() or bool(self.external_pids())
        return bool(self.external_pids())
    def status(self):
        running=self.running();owned=self.process is not None and self.process.poll() is None
        eligible=False
        if running and self.adopt and not owned:
            pids=self.external_pids()
            try:eligible=len(pids)==1 and (Path('/proc')/str(pids[0])).stat().st_uid==os.geteuid()
            except OSError:pass
        can_stop=running and self.mode!='disabled' and (self.mode=='systemd_user' or owned or eligible)
        return {'ok':True,'mode':self.mode,'running':running,'processIdentity':self.process_identity(),'processStartedAt':self.process_started_at(),'auditError':self.audit_error,'owned':owned,'adoptExisting':self.adopt,
                'canStart':self.mode!='disabled' and not running,'canStop':can_stop,
                'restartConfigured':self.mode!='disabled','settingsConfigured':self.ini is not None and self.ini.is_file(),'logsConfigured':self.log is not None and self.log.is_file(),
                'updates':self.update_status(),'jobs':self.job_list()}
    def process_identity(self):
        pids=self.external_pids()
        if len(pids)!=1:return ''
        try:
            fields=Path('/proc',str(pids[0]),'stat').read_text().rsplit(')',1)[1].split()
            boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
            return boot+':'+str(pids[0])+':'+fields[19]
        except (OSError,IndexError):return ''
    def process_started_at(self):
        pids=self.external_pids()
        if len(pids)!=1:return None
        try:
            fields=Path('/proc',str(pids[0]),'stat').read_text().rsplit(')',1)[1].split()
            uptime=float(Path('/proc/uptime').read_text().split()[0])
            return time.time()-uptime+int(fields[19])/os.sysconf('SC_CLK_TCK')
        except (OSError,IndexError,ValueError):return None
    def job_list(self):
        with self.jobs_lock:return list(self.jobs.values())[-30:]
    def _start(self):
        if self.update_marker.exists():raise ValueError('Previous server update did not complete; retry the update before starting')
        if self.mode=='disabled':raise ValueError('Process control is not configured')
        if self.running():raise ValueError('A game process is already running; no second instance started')
        if self.mode=='systemd_user':
            result=subprocess.run(['systemctl','--user','start','--',self.unit],capture_output=True,timeout=60)
            if result.returncode:raise ValueError('User service start failed; inspect its journal')
        else:
            if self.output.exists() and self.output.stat().st_size>20*1024*1024:
                self.output.rename(self.backups/('process-output-'+uuid.uuid4().hex+'.log'))
            fd=os.open(self.output,os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_NOFOLLOW,0o600)
            with os.fdopen(fd,'ab',buffering=0) as output:
                os.fchmod(output.fileno(),0o600)
                # A regular file outlives the agent. No broken stdout pipe can
                # terminate the game when the controller exits or is restarted.
                self.process=subprocess.Popen(self.command,cwd=self.root,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
        return 'Start requested; verify the game becomes ready'
    def _stop(self):
        if self.mode=='disabled':raise ValueError('Process control is not configured')
        deadline=time.monotonic()+60
        if self.mode=='systemd_user':
            result=subprocess.run(['systemctl','--user','stop','--',self.unit],capture_output=True,timeout=60)
            if result.returncode:raise ValueError('User service stop failed; inspect its journal')
        elif self.process is None or self.process.poll() is not None:
            pids=self.external_pids()
            if not pids:return 'Already stopped'
            if not self.adopt:raise ValueError('The running game was not launched by this service; refusing to stop it')
            if len(pids)!=1:raise ValueError('Multiple matching game processes; no process was signalled')
            pid=pids[0];fd=os.pidfd_open(pid,0)
            try:
                proc=Path('/proc')/str(pid)
                if proc.stat().st_uid!=os.geteuid() or (proc/'exe').resolve(strict=True)!=self.exe:raise ValueError('Game process ownership or executable changed; no signal sent')
                # Stable process handle: no PID reuse, no SSH process-group kill.
                signal.pidfd_send_signal(fd,signal.SIGTERM)
                if not select.select([fd],[],[],max(0,deadline-time.monotonic()))[0]:raise ValueError('Game did not exit within 60 seconds; no forced kill or restart performed')
            finally:os.close(fd)
        else:
            os.killpg(self.process.pid,signal.SIGTERM)
            try:self.process.wait(timeout=max(.1,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:raise ValueError('Game did not stop cleanly; no forced kill or restart was performed')
            if self.process.stdin:self.process.stdin.close()
        while self.external_pids():
            if time.monotonic()>=deadline:raise ValueError('A matching game process is still running; restart refused')
            time.sleep(.1)
        return 'Stop completed; game process exit observed'
    def list_backups(self):
        rows=[]
        for path in sorted(self.backups.glob('*.json'),reverse=True):
            try:
                row=json.loads(path.read_text())
                if re.fullmatch(r'[0-9]{14}-[a-f0-9]{8}',row.get('id','')):rows.append({k:row[k] for k in ('id','time','bytes','sha256','files')})
            except (OSError,ValueError,KeyError):pass
        return rows[:100]
    def backup(self):
        if self.running():raise ValueError('Stop the game before a consistent save backup; no live backup was attempted')
        if not self.saves.is_dir():raise ValueError('Configured save directory does not exist')
        files=[];total=0
        for path in sorted(self.saves.rglob('*')):
            if path.is_symlink():raise ValueError('Save symlinks are not backed up')
            if not path.is_file():continue
            rel=path.relative_to(self.saves).as_posix()
            if rel=='__THC_MANIFEST__.json':raise ValueError('Reserved manifest name in save directory')
            total+=path.stat().st_size
            if total>self.max_bytes or len(files)>=10000:raise ValueError('Backup size/file limit exceeded')
            files.append((path,rel))
        ident=time.strftime('%Y%m%d%H%M%S',time.gmtime())+'-'+uuid.uuid4().hex[:8]
        tmp=self.backups/(ident+'.zip.tmp');final=self.backups/(ident+'.zip')
        try:
            manifest={rel:digest(path) for path,rel in files}
            with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED) as archive:
                for path,rel in files:archive.write(path,rel)
                archive.writestr('__THC_MANIFEST__.json',json.dumps(manifest))
            with zipfile.ZipFile(tmp) as archive:
                if archive.testzip() is not None:raise ValueError('Backup verification failed')
            os.replace(tmp,final)
            row={'id':ident,'time':int(time.time()),'bytes':final.stat().st_size,'sha256':digest(final),'files':len(files)}
            atomic_json(self.backups/(ident+'.json'),row)
            return 'Verified backup '+ident
        finally:
            if tmp.exists():tmp.unlink()
    def restore(self,ident):
        if self.running():raise ValueError('Stop the game before restore; saves were not changed')
        if not isinstance(ident,str) or not re.fullmatch(r'[0-9]{14}-[a-f0-9]{8}',ident):raise ValueError('Invalid backup ID')
        path=self.backups/(ident+'.zip');meta=json.loads((self.backups/(ident+'.json')).read_text())
        if digest(path)!=meta['sha256']:raise ValueError('Backup checksum mismatch')
        staging=Path(tempfile.mkdtemp(dir=self.saves.parent,prefix='.thc-restore-'))
        try:
            with zipfile.ZipFile(path) as archive:
                entries=archive.infolist();total=0
                if len(entries)>10001:raise ValueError('Too many archive entries')
                for item in entries:
                    name=PurePosixPath(item.filename);total+=item.file_size
                    if name.is_absolute() or '..' in name.parts or '\\' in item.filename or ':' in item.filename or stat.S_ISLNK(item.external_attr>>16) or total>self.max_bytes:
                        raise ValueError('Unsafe archive entry')
                manifest=json.loads(archive.read('__THC_MANIFEST__.json'))
                expected=set(manifest)|{'__THC_MANIFEST__.json'}
                if len({x.filename for x in entries})!=len(entries) or {x.filename for x in entries}!=expected:raise ValueError('Archive manifest mismatch')
                for item in entries:
                    if item.filename=='__THC_MANIFEST__.json':continue
                    target=staging/item.filename;target.parent.mkdir(parents=True,exist_ok=True)
                    with archive.open(item) as source,open(target,'wb') as dest:shutil.copyfileobj(source,dest)
                    if digest(target)!=manifest[item.filename]:raise ValueError('Save checksum mismatch')
            # Preserve the current saves as a separate directory, then swap on
            # the same filesystem. Never overlay an archive onto live files.
            previous=self.saves.with_name(self.saves.name+'.thc-before-restore-'+uuid.uuid4().hex[:8])
            if self.saves.exists():self.saves.rename(previous)
            try:staging.rename(self.saves)
            except BaseException:
                if previous.exists():previous.rename(self.saves)
                raise
            return 'Restore completed; previous saves retained as '+previous.name
        finally:
            if staging.exists():shutil.rmtree(staging)
    def logs(self):
        log=self.log if self.log else (self.output if self.output.exists() else None)
        if log:
            with open(log,'rb') as f:f.seek(0,2);size=f.tell();f.seek(max(0,size-128*1024));text=f.read(128*1024).decode(errors='replace')
            return redacted(text).splitlines()[-300:]
        return []
    def settings(self):
        if not self.ini:raise ValueError('Game settings file is not configured')
        rows={}
        for line in self.ini.read_text().splitlines():
            match=re.match(r'\s*(ServerName|MaxPlayers|WorldPassword|ServerPassword)\s*=\s*(.*)',line)
            if match:rows[match[1]]=bool(match[2]) if 'Password' in match[1] else match[2]
        return rows
    def update_settings(self,values):
        if not self.ini or not isinstance(values,dict):raise ValueError('Game settings file is not configured')
        allowed={'ServerName','MaxPlayers','WorldPassword','ServerPassword'}
        if not values or not set(values)<=allowed:raise ValueError('Unsupported setting')
        for key,value in values.items():
            if key=='MaxPlayers':
                if not isinstance(value,int) or isinstance(value,bool) or not 1<=value<=64:raise ValueError('MaxPlayers must be 1 to 64; game limits still apply')
            elif not isinstance(value,str) or len(value)>128 or any(c in value for c in '\r\n\x00'):raise ValueError('Invalid setting text')
        lines=self.ini.read_text().splitlines(keepends=True);found=set()
        for i,line in enumerate(lines):
            match=re.match(r'(\s*)(ServerName|MaxPlayers|WorldPassword|ServerPassword)\s*=',line)
            if match and match[2] in values:found.add(match[2]);lines[i]=match[1]+match[2]+'='+str(values[match[2]])+'\n'
        if found!=set(values):raise ValueError('Setting does not exist in the game configuration; no guessed key was written')
        shutil.copy2(self.ini,self.backups/('settings-before-'+uuid.uuid4().hex+'.ini'))
        fd,tmp=tempfile.mkstemp(dir=self.ini.parent,prefix='.thc-settings-')
        try:
            with os.fdopen(fd,'w') as f:f.writelines(lines);f.flush();os.fsync(f.fileno())
            os.replace(tmp,self.ini)
        finally:
            if os.path.exists(tmp):os.unlink(tmp)
        return 'Game settings saved; game restart required'
    def update_status(self):
        release=None;error=''
        if self.dashboard_release:
            try:release=self.dashboard_bundle()[0]['version']
            except (OSError,ValueError,KeyError,TypeError) as exc:error=redacted(str(exc))
        return {'serverConfigured':bool(self.steamcmd and Path(self.steamcmd).is_file() and os.access(self.steamcmd,os.X_OK)),
                'serverNeedsRetry':self.update_marker.exists(),'dashboardVersion':release,'dashboardError':error,
                'dashboardRollbackAvailable':bool(self.dashboard_file and (self.dashboard_file.parent/'dashboard-previous/manifest.json').is_file())}
    def update_server(self):
        if self.running():raise ValueError('Stop the game before updating; no update was attempted')
        if not self.steamcmd or not Path(self.steamcmd).is_file() or not os.access(self.steamcmd,os.X_OK):raise ValueError('SteamCMD is not configured or executable')
        backup=self.backup()
        atomic_json(self.update_marker,{'startedAt':int(time.time()),'saveBackup':backup})
        env={k:v for k,v in os.environ.items() if k not in ('LD_PRELOAD','LD_AUDIT')}
        command=[self.steamcmd,'+force_install_dir',str(self.root),'+login','anonymous','+app_update','4019830','validate','+quit']
        fd=os.open(self.update_log,os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)
        with os.fdopen(fd,'wb') as output:
            process=subprocess.Popen(command,cwd=self.root,stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,env=env,start_new_session=True)
            try:code=process.wait(timeout=self.update_timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGTERM)
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
                raise ValueError('SteamCMD timed out; game remains stopped. Retry update; see update log')
        with self.update_log.open('rb') as log:
            log.seek(max(0,self.update_log.stat().st_size-128*1024));tail=log.read().decode(errors='replace')
        if code!=0 or not re.search(r"Success! App ['\"]?4019830['\"]? fully installed",tail):
            raise ValueError('SteamCMD did not confirm app 4019830 installed; game remains stopped. Retry update; see update log')
        if not self.exe or not self.exe.is_file():raise ValueError('Game executable missing after update; game remains stopped')
        self.update_marker.unlink()
        return backup+'; SteamCMD verified app 4019830. Game remains stopped; start it when ready'
    def dashboard_bundle(self,directory=None):
        directory=directory or self.dashboard_release
        if not directory or not self.dashboard_file:raise ValueError('No dashboard release directory configured')
        manifest_path=directory/'manifest.json'
        if manifest_path.is_symlink() or manifest_path.stat().st_size>16384:raise ValueError('Invalid dashboard manifest')
        manifest=json.loads(manifest_path.read_text())
        version=manifest.get('version');files=manifest.get('files')
        allowed={'dashboard.html':1024*1024,'thc-banner.png':10*1024*1024,'thc-community.png':10*1024*1024}
        if not isinstance(version,str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}',version) or not isinstance(files,dict) or 'dashboard.html' not in files or not set(files)<=set(allowed):raise ValueError('Invalid dashboard release manifest')
        content={}
        for name,checksum in files.items():
            file=directory/name
            if file.is_symlink() or not file.is_file() or file.stat().st_size>allowed[name]:raise ValueError('Invalid dashboard release file: '+name)
            data=file.read_bytes()
            if len(data)>allowed[name] or not isinstance(checksum,str) or not hmac.compare_digest(hashlib.sha256(data).hexdigest(),checksum):raise ValueError('Dashboard checksum mismatch: '+name)
            if name=='dashboard.html':
                text=data.decode('utf-8')
                if '<html' not in text.lower() or '</html>' not in text.lower():raise ValueError('Dashboard HTML is incomplete')
            elif not data.startswith(b'\x89PNG\r\n\x1a\n'):raise ValueError('Dashboard asset is not a PNG')
            content[name]=data
        return manifest,content
    def dashboard_apply(self,rollback=False):
        if not self.dashboard_file:raise ValueError('Dashboard path is not configured')
        previous=self.dashboard_file.parent/'dashboard-previous'
        manifest,content=self.dashboard_bundle(previous if rollback else None)
        # Stage and validate all bytes before changing any served file. Only static UI files are allowed.
        before={name:(self.dashboard_file.parent/name).read_bytes() if (self.dashboard_file.parent/name).is_file() else None for name in content}
        if not rollback:
            previous.mkdir(mode=0o700,exist_ok=True)
            hashes={}
            for name,data in before.items():
                if data is not None:self.write_static(previous/name,data);hashes[name]=hashlib.sha256(data).hexdigest()
            atomic_json(previous/'manifest.json',{'version':'previous','files':hashes})
        try:
            for name,data in content.items():self.write_static(self.dashboard_file.parent/name,data)
        except Exception:
            for name,data in before.items():
                if data is not None:self.write_static(self.dashboard_file.parent/name,data)
                else:(self.dashboard_file.parent/name).unlink(missing_ok=True)
            raise
        return ('Previous dashboard restored' if rollback else 'Dashboard '+manifest['version']+' installed')+'; refresh this browser tab'
    @staticmethod
    def write_static(path,data):
        fd,name=tempfile.mkstemp(dir=path.parent,prefix='.thc-ui-')
        try:
            with os.fdopen(fd,'wb') as file:file.write(data);file.flush();os.fsync(file.fileno())
            os.replace(name,path)
        finally:
            if os.path.exists(name):os.unlink(name)
    def execute(self,action,request):
        if action=='update_server':return self.update_server()
        if action=='update_dashboard':return self.dashboard_apply()
        if action=='rollback_dashboard':return self.dashboard_apply(True)
        if action=='start':return self._start()
        if action=='stop':return self._stop()
        if action=='restart':self._stop();return self._start()
        if action=='backup':return self.backup()
        if action=='restore':return self.restore(request.get('id'))
        if action=='update_game_settings':return self.update_settings(request.get('values'))
        raise ValueError('Unsupported action')
    def dispatch(self,request,actor='local service'):
        actor=dict(actor) if isinstance(actor,dict) else str(actor)
        action=request.get('action')
        if action=='status':return self.status()
        if action=='backups':return {'ok':True,'backups':self.list_backups()}
        if action=='logs':return {'ok':True,'lines':self.logs()}
        if action=='update_log':
            if not self.update_log.is_file():return {'ok':True,'lines':[]}
            with self.update_log.open('rb') as file:
                file.seek(max(0,self.update_log.stat().st_size-128*1024));lines=file.read().decode(errors='replace').splitlines()[-300:]
            return {'ok':True,'lines':[redacted(line) for line in lines]}
        if action=='game_settings':return {'ok':True,'settings':self.settings()}
        if action not in {'start','stop','restart','backup','restore','update_game_settings','update_server','update_dashboard','rollback_dashboard'}:raise ValueError('Unsupported action')
        if not self.lock.acquire(blocking=False):raise ValueError('Another control operation is running')
        ident=uuid.uuid4().hex
        with self.jobs_lock:
            if len(self.jobs)>=100:
                oldest=next(iter(self.jobs));self.jobs.pop(oldest)
            self.jobs[ident]={'id':ident,'action':action,'state':'running','message':'Operation accepted','actor':actor}
        self.audit('control:'+action,'accepted',{'job':ident},actor)
        def run():
            time.sleep(.25)  # Allow the mod to return the accepted-job response first.
            try:message=self.execute(action,request);state='complete'
            except Exception as error:message=redacted(str(error));state='failed'
            finally:
                with self.jobs_lock:self.jobs[ident].update(state=state,message=message)
                self.audit('control:'+action,state,{'job':ident,'result':'Completed' if state=='complete' else 'Failed; inspect Operation Jobs'},actor)
                self.lock.release()
        threading.Thread(target=run,daemon=True).start()
        return {'ok':True,'job':ident,'message':'Operation accepted; inspect job status for completion'}

class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            if hasattr(socket,'SO_PEERCRED'):
                import struct
                _,uid,_=struct.unpack('3i',self.request.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                if uid not in (os.getuid(),0):raise ValueError('Peer UID not allowed')
            self.request.settimeout(10);line=self.rfile.readline(65537)
            if len(line)>65536 or not line.endswith(b'\n'):raise ValueError('Request too large')
            body=json.loads(line)
            if not isinstance(body,dict):raise ValueError('Object required')
            result=self.server.controller.dispatch(body,actor=body.get('_auditActor','local service'))
        except Exception as error:result={'ok':False,'error':redacted(str(error))}
        self.wfile.write((json.dumps(result)+'\n').encode())
class Server(socketserver.ThreadingUnixStreamServer):
    daemon_threads=True

class SharedMap:
    MAX_BODY = 14 * 1024 * 1024

    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.Lock()
        self.cached = None
        self.signature = None

    @staticmethod
    def validate(value):
        if not isinstance(value, dict) or value.get('schema') != 1:
            raise ValueError('Unsupported map profile')
        image = value.get('image', {})
        if not isinstance(image, dict) or image.get('mime') not in ('image/png', 'image/jpeg', 'image/webp') or not isinstance(image.get('data'), str):
            raise ValueError('Map image must be PNG, JPEG or WebP')
        try:
            raw = base64.b64decode(image['data'], validate=True)
        except ValueError:
            raise ValueError('Invalid map image encoding')
        if not 30 <= len(raw) <= 10 * 1024 * 1024:
            raise ValueError('Use a map image below 10 MB')
        width = height = 0
        if image['mime'] == 'image/png' and raw[:8] == b'\x89PNG\r\n\x1a\n' and raw[12:16] == b'IHDR':
            width, height = struct.unpack('>II', raw[16:24])
        elif image['mime'] == 'image/webp' and raw[:4] == b'RIFF' and raw[8:12] == b'WEBP':
            if raw[12:16] == b'VP8X':
                width = int.from_bytes(raw[24:27], 'little') + 1
                height = int.from_bytes(raw[27:30], 'little') + 1
            elif raw[12:16] == b'VP8L' and raw[20] == 0x2f:
                bits = int.from_bytes(raw[21:25], 'little')
                width, height = (bits & 0x3fff) + 1, ((bits >> 14) & 0x3fff) + 1
            elif raw[12:16] == b'VP8 ' and raw[23:26] == b'\x9d\x01\x2a':
                width, height = struct.unpack('<HH', raw[26:30])
                width, height = width & 0x3fff, height & 0x3fff
        elif image['mime'] == 'image/jpeg' and raw[:2] == b'\xff\xd8':
            offset = 2
            while offset + 4 <= len(raw):
                if raw[offset] != 0xff:
                    break
                while offset < len(raw) and raw[offset] == 0xff:
                    offset += 1
                if offset >= len(raw):
                    break
                marker = raw[offset];offset += 1
                if marker in (0xd9, 0xda):
                    break
                if marker == 0x01 or 0xd0 <= marker <= 0xd8:
                    continue
                if offset + 2 > len(raw):
                    break
                size = int.from_bytes(raw[offset:offset+2], 'big')
                if size < 2 or offset + size > len(raw):
                    break
                if marker in (0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf) and size >= 7:
                    height, width = struct.unpack('>HH', raw[offset+3:offset+7])
                    break
                offset += size
        if not 0 < width <= 16384 or not 0 < height <= 16384 or width * height > 64000000:
            raise ValueError('Map image dimensions are too large')

        def finite(n, limit=1e9):
            return type(n) in (int, float) and math.isfinite(n) and abs(n) <= limit

        def anchors(rows):
            if not isinstance(rows, list) or len(rows) > 3:
                raise ValueError('Invalid map landmarks')
            cleaned = []
            for row in rows:
                if not isinstance(row, dict) or not all(finite(row.get(k)) for k in ('x', 'y')) or not all(finite(row.get(k), 1) and 0 <= row[k] <= 1 for k in ('u', 'v')):
                    raise ValueError('Invalid map landmark coordinates')
                point = {k: row[k] for k in ('x', 'y', 'u', 'v')}
                if 'z' in row:
                    if not finite(row['z']):
                        raise ValueError('Invalid landmark height')
                    point['z'] = row['z']
                cleaned.append(point)
            return cleaned

        config = value.get('config')
        points = anchors(value.get('anchors', []))
        if not isinstance(config, dict):
            raise ValueError('Calibrate the map before publishing')
        if 'anchors' in config:
            points = anchors(config['anchors'])
            if len(points) != 3:
                raise ValueError('Three landmarks are required')
            p, q, r = points
            dx, dy, ex, ey = q['x']-p['x'], q['y']-p['y'], r['x']-p['x'], r['y']-p['y']
            det = dx*ey-ex*dy
            image_det = (q['u']-p['u'])*(r['v']-p['v'])-(r['u']-p['u'])*(q['v']-p['v'])
            if abs(det) <= max(1, math.hypot(dx, dy)*math.hypot(ex, ey))*1e-5 or abs(image_det) < .0001:
                raise ValueError('Landmarks are too close together or lie on a line')
            config = {'anchors': points}
        else:
            keys = ('minX', 'maxX', 'minY', 'maxY')
            if not all(finite(config.get(k)) for k in keys) or config['minX'] >= config['maxX'] or config['minY'] >= config['maxY']:
                raise ValueError('Invalid coordinate bounds')
            config = {k: config[k] for k in keys}
        return {'schema': 1, 'config': config, 'anchors': points, 'image': {'mime': image['mime'], 'data': image['data']}}

    def _read(self):
        if self.path.is_symlink():
            raise ValueError('Refusing a linked map profile')
        if not self.path.exists():
            self.cached = self.signature = None
            return None
        info = self.path.stat()
        signature = (info.st_ino, info.st_size, info.st_mtime_ns)
        if signature == self.signature:
            return self.cached
        if info.st_size > self.MAX_BODY:
            raise ValueError('Stored map exceeds size limit')
        record = json.loads(self.path.read_text())
        self.validate(record['profile'])
        if not isinstance(record.get('revision'), str):
            raise ValueError('Invalid stored map revision')
        self.cached, self.signature = record, signature
        return record

    def read(self, metadata=False):
        with self.lock:
            record = self._read()
            if record is None:
                return {'configured': False, 'revision': ''}
            result = {'configured': True, 'revision': record['revision'], 'updatedAt': record['updatedAt']}
            if not metadata:
                result['profile'] = record['profile']
            return result

    def publish(self, value):
        profile = self.validate(value.get('profile'))
        with self.lock:
            previous = self._read()
            revision = previous['revision'] if previous else ''
            if value.get('baseRevision') != revision:
                raise ValueError('Shared map changed; load it before publishing again')
            # Preserve the previous profile for rollback. Neither file contains credentials.
            if previous:
                atomic_json(self.path.with_suffix('.previous.json'), previous)
            record = {'revision': uuid.uuid4().hex, 'updatedAt': int(time.time()), 'profile': profile}
            atomic_json(self.path, record)
            return {'configured': True, 'revision': record['revision'], 'message': 'Shared map saved for everyone'}


class DashboardProxy:
    def __init__(self,controller,config):
        self.controller=controller
        self.api_config=controller.inside(config['api_config'])
        self.access_file=self.api_config.parent/'thc/access.json'
        self.dashboard=Path(config['dashboard_file']).resolve(strict=True)
        if self.dashboard.stat().st_size>1024*1024:raise ValueError('Dashboard file exceeds size limit')
        self.opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.last_version='game offline'
        self.shared_map=SharedMap(self.api_config.parent/'thc/shared-map.json')
    def api_settings(self):
        parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str
        if self.api_config.stat().st_size>128*1024:raise ValueError('API configuration exceeds size limit')
        parser.read(self.api_config)
        token=parser.get('API','BearerToken',fallback='')
        port=parser.getint('API','Port',fallback=8081)
        if not 1<=port<=65535:raise ValueError('Invalid local API port')
        return token,port
    def identity(self,token):
        try:
            owner,_=self.api_settings()
            if not owner:return None
            if hmac.compare_digest(token,owner):return {'id':'owner','label':'Owner','role':'admin'}
            if self.access_file.exists() and self.access_file.stat().st_size<=128*1024:
                rows=json.loads(self.access_file.read_text())
                if isinstance(rows,list):
                    for row in rows:
                        if hmac.compare_digest(token,row['token']) and row['role'] in ('viewer','operator','admin'):
                            return {key:row[key] for key in ('id','label','role')}
        except (OSError,ValueError,KeyError,TypeError):pass
        return None
    def role(self,token):
        identity=self.identity(token)
        return identity['role'] if identity else None
    def forward(self,path,token,body,metadata=None):
        _,port=self.api_settings()
        request=urllib.request.Request('http://127.0.0.1:'+str(port)+path,data=body,
          headers={'Authorization':'Bearer '+token,**({'Content-Type':'application/json'} if body is not None else {})})
        try:
            with self.opener.open(request,timeout=15) as response:
                if metadata is not None:metadata['nativeAuditQueued']=response.headers.get('X-THC-Audit-Queued')=='1'
                data=response.read(2*1024*1024)
                if path=='/api/health':
                    try:
                        health=json.loads(data);self.last_version=health.get('version',self.last_version);health['controlAvailable']=True;health['sharedMapAvailable']=True;health['inviteCodeAvailable']=True;data=json.dumps(health).encode()
                    except (ValueError,AttributeError):pass
                return response.status,data
        except urllib.error.HTTPError as error:
            if metadata is not None:metadata['nativeAuditQueued']=(error.headers or {}).get('X-THC-Audit-Queued')=='1'
            return error.code,error.read(65536)
        except (OSError,ValueError):
            if body is None and path=='/api/health':
                return 200,json.dumps({'status':'offline','version':self.last_version,'engineReady':False,'uptimeSeconds':0,'broadcastReady':False,'controlAvailable':True,'sharedMapAvailable':True,'inviteCodeAvailable':True}).encode()
            if body is None and path=='/api/thc/status':
                running=self.controller.running()
                return 200,json.dumps({'started':False,'worldObserved':False,'gameOffline':not running,'monitoringUnavailable':True,'ledger':{'loaded':False},'administration':{},'system':{},'discord':{}}).encode()
            return 503,b'{"error":"Game API unavailable; local control remains available"}'

class DashboardHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def reply(self,status,data,content='application/json'):
        context=getattr(self,'audit_context',None)
        if context:
            self.audit_context=None
            # Accepted control jobs have their own accepted/completed audit records.
            if not (context['path']=='/api/thc/control' and status==200 and isinstance(data,dict) and data.get('job')):
                outcome='accepted (completion separate)' if status==202 else 'HTTP '+str(status)
                self.server.proxy.controller.audit(context['path'],outcome,context['details'],context['actor'])
        if not isinstance(data,bytes):data=json.dumps(data).encode()
        self.send_response(status);self.send_header('Content-Type',content);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('Connection','close');self.end_headers();self.wfile.write(data)
    def do_GET(self):self.handle_request(False)
    def do_POST(self):self.handle_request(True)
    def handle_request(self,post):
        self.audit_context=None
        proxy=self.server.proxy;path=urllib.parse.urlsplit(self.path).path
        try:
            self.connection.settimeout(20)
            if not post and path in ('/assets/thc-banner.png','/assets/thc-community.png'):
                banner=proxy.dashboard.with_name('thc-community.png' if path.endswith('thc-community.png') else 'thc-banner.png')
                if not banner.is_file() or banner.stat().st_size>10*1024*1024:self.reply(404,{'error':'Banner unavailable'});return
                self.reply(200,banner.read_bytes(),'image/png');return
            if not post and path in ('/','/dashboard'):
                self.reply(200,proxy.dashboard.read_bytes(),'text/html; charset=utf-8');return
            auth=self.headers.get_all('Authorization',[])
            token=auth[0][7:] if len(auth)==1 and auth[0].startswith('Bearer ') else ''
            identity=proxy.identity(token)
            role=identity['role'] if identity else None
            if not role:self.reply(401,{'error':'Unauthorized'});return
            if post:self.audit_context={'path':path.split('?',1)[0][:128],'details':{},'actor':dict(identity)}
            if post and path=='/api/thc/map' and role!='admin':
                self.reply(403,{'error':'Admin access required'});return
            body=None;value=None
            if post:
                lengths=self.headers.get_all('Content-Length',[])
                if len(lengths)!=1 or self.headers.get('Transfer-Encoding'):raise ValueError('Content length required')
                size=int(lengths[0])
                limit=SharedMap.MAX_BODY if path=='/api/thc/map' else 65536
                if not 0<=size<=limit:raise ValueError('Body too large')
                body=self.rfile.read(size)
                if len(body)!=size:raise ValueError('Incomplete body')
                value=json.loads(body)
                if not isinstance(value,dict):raise ValueError('JSON object required')
                self.audit_context['details']=audit_details(path,value)
            if path=='/api/thc/access/me' and not post:self.reply(200,identity);return
            if path=='/api/thc/invite' and not post:
                result={'available':False,'code':'','message':'Waiting for an invite code from the current game session'}
                state=proxy.api_config.parent/'thc/error-watch.json'
                if state.is_file() and state.stat().st_size<=16*1024*1024:
                    invite=json.loads(state.read_text()).get('invite',{})
                    identity=proxy.controller.process_identity()
                    log=proxy.controller.log
                    current_log=log.stat() if log and log.is_file() else None
                    log_identity=f'{current_log.st_dev}:{current_log.st_ino}' if current_log else ''
                    code=invite.get('code','')
                    if identity and invite.get('valid') is True and invite.get('processIdentity')==identity and invite.get('logIdentity')==log_identity and isinstance(code,str) and re.fullmatch(r'[A-Za-z0-9]{4}-[A-Za-z0-9]{4}',code):
                        result={'available':True,'code':code,'observedAt':invite.get('observedAt'),'message':'Code observed in current session log; joinability is not independently tested'}
                self.reply(200,result);return
            if path in ('/api/thc/map','/api/thc/map/status'):
                if post:
                    if path!='/api/thc/map':raise ValueError('Unsupported map route')
                    result=proxy.shared_map.publish(value)
                else:result=proxy.shared_map.read(metadata=path.endswith('/status'))
                self.reply(200,result);return
            if path.startswith('/api/thc/control'):
                if role!='admin':self.reply(403,{'error':'Admin access required'});return
                if post:
                    if path!='/api/thc/control':raise ValueError('Unsupported control route')
                    result=proxy.controller.dispatch(value,actor=identity)
                else:
                    action={'/api/thc/control':'status','/api/thc/control/backups':'backups','/api/thc/control/logs':'logs','/api/thc/control/game-settings':'game_settings','/api/thc/control/update-log':'update_log'}.get(path)
                    if not action:raise ValueError('Unsupported control route')
                    result=proxy.controller.dispatch({'action':action})
                self.reply(200,result);return
            if not path.startswith('/api/') or self.path.startswith('//'):self.reply(404,{'error':'Not found'});return
            # Preserve the caller credential: the mod applies its own role policy.
            metadata={}
            status,data=proxy.forward(self.path,token,body,metadata)
            if self.audit_context is not None and metadata.get('nativeAuditQueued'):
                self.audit_context['details']['nativeAuditQueued']=True
            self.reply(status,data)
        except (OSError,ValueError,KeyError,TypeError) as error:self.reply(409 if path.startswith('/api/thc/control') else 400,{'error':redacted(str(error))})

@contextlib.contextmanager
def socket_lock(sock):
    """Serialize startup and replace only our owned, proven-stale Unix socket."""
    lock_path=str(sock)+'.lock';fd=os.open(lock_path,os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    try:
        try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise ValueError('Another control agent owns this socket lock')
        if sock.exists() or sock.is_symlink():
            previous=sock.lstat()
            if not stat.S_ISSOCK(previous.st_mode) or previous.st_uid!=os.geteuid():raise ValueError('Refusing to replace a non-socket or differently owned path')
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as probe:
                probe.settimeout(1)
                try:probe.connect(str(sock))
                except ConnectionRefusedError:
                    current=sock.lstat()
                    if (current.st_dev,current.st_ino)!=(previous.st_dev,previous.st_ino):raise ValueError('Socket changed during startup; preserving it')
                    sock.unlink()
                except FileNotFoundError:pass
                else:raise ValueError('An existing listener is using the control socket')
        yield
    finally:os.close(fd)

def main():
    os.umask(0o077);parser=argparse.ArgumentParser();parser.add_argument('--config',required=True);args=parser.parse_args()
    config=json.loads(Path(args.config).read_text());controller=Controller(config);sock=Path(config['socket'])
    sock.parent.mkdir(parents=True,exist_ok=True);os.chmod(sock.parent,0o700)
    guard=socket_lock(sock);guard.__enter__()
    web=None
    try:
        if config.get('http_port'):
            if config.get('http_bind','127.0.0.1')!='127.0.0.1':raise ValueError('Dashboard binds to localhost only')
            web=http.server.ThreadingHTTPServer(('127.0.0.1',int(config['http_port'])),DashboardHandler)
            web.proxy=DashboardProxy(controller,config)
            threading.Thread(target=web.serve_forever,daemon=True).start()
        with Server(str(sock),Handler) as server:
            os.chmod(sock,0o600);server.controller=controller;server.serve_forever()
    finally:
        if web:web.shutdown();web.server_close()
        if sock.exists():sock.unlink()
        guard.__exit__(None,None,None)
if __name__=='__main__':main()
