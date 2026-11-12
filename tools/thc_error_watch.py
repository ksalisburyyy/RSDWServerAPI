#!/usr/bin/env python3
"""Independent read-only game/API log -> admin Discord observer. No process control."""
import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ERROR = re.compile(r'\b(?:Fatal|Critical error)\s*:|\bFatal error\b|\bUnhandled Exception\b|\bSIG(?:SEGV|ABRT|BUS|ILL)\b|\bCaught signal (?:4|6|7|11)\b|segmentation fault|\b(?:assertion|check|checkf) failed\b|\b(?:out of memory|ran out of memory|OutOfMemory|OOM killer)\b', re.I)
WARNING = re.compile(r'\bWarning\s*:', re.I)
WEATHER_CACHE = re.compile(r'MaxWeatherCacheSize|weather[ _-]*cache', re.I)


def crash_evidence(text):
    if WEATHER_CACHE.search(text):return False
    severity=re.search(r'\b(Error|Warning|Fatal|Critical error)\s*:',text,re.I)
    return not (severity and severity.group(1).lower()=='warning') and bool(ERROR.search(text))


def atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.thc-error-watch-')
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)


def redact(text, secrets=()):
    text = re.sub(r'https://(?:\w+\.)?discord(?:app)?\.com/api/webhooks/\S+', '[webhook redacted]', text, flags=re.I)
    text = re.sub(r'(?i)\bBearer\s+\S+', 'Bearer [redacted]', text)
    text = re.sub(r'(?i)((?:BearerToken|WorldPassword|ServerPassword|Password|Token)\s*[=:]\s*)[^\s,;]+', r'\1[redacted]', text)
    for secret in sorted((x for x in secrets if x), key=len, reverse=True):text = text.replace(secret, '[redacted]')
    # Plain text payloads never allow mentions; strip other control characters.
    return ''.join(c for c in text if c in '\n\t' or ord(c) >= 32)[:1500]


def route(url, thread):
    if not url:return ''
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.hostname not in ('discord.com','discordapp.com','canary.discord.com','ptb.discord.com') or parsed.username or parsed.password or parsed.fragment or parsed.port not in (None,443) or not re.fullmatch(r'/api/webhooks/[0-9]+/[^/\s]+', parsed.path):
        raise ValueError('Invalid Discord webhook configuration')
    query = urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
    if thread:
        if not re.fullmatch(r'[1-9][0-9]{15,19}',thread) or int(thread)>2**64-1:raise ValueError('Invalid Discord thread ID')
        query = [(k,v) for k,v in query if k!='thread_id'] + [('thread_id',thread)]
    query = [(k,v) for k,v in query if k!='wait'] + [('wait','true')]
    return urllib.parse.urlunsplit((parsed.scheme,parsed.netloc,parsed.path,urllib.parse.urlencode(query),''))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None


def post(url, payload):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    request = urllib.request.Request(url, data=json.dumps(payload).encode(),headers={'Content-Type':'application/json','User-Agent':'THC-Error-Watcher/1'},method='POST')
    try:
        with opener.open(request,timeout=8) as response:return response.status,0
    except urllib.error.HTTPError as error:
        delay = 0
        try:
            delay = float(error.headers.get('Retry-After','0'))
            body = json.loads(error.read(4096))
            delay = max(delay,float(body.get('retry_after',0)))
        except (ValueError,TypeError,AttributeError):pass
        return error.code,min(86400,max(0,delay))
    except (OSError,ValueError):return 0,1


class Watcher:
    def __init__(self, path):
        self.path = Path(path)
        if self.path.exists():
            if self.path.stat().st_size>16*1024*1024:raise ValueError('Watcher state exceeds limit; preserved')
            self.state = json.loads(self.path.read_text())
            if self.state.get('schema')!=1 or not all(isinstance(self.state.get(k),t) for k,t in [('files',dict),('pending',list),('groups',dict)]):raise ValueError('Invalid watcher state; preserved')
        else:self.state={'schema':1,'next':1,'files':{},'pending':[],'groups':{},'sent':0,'lastHttpStatus':0,'lastError':'','process':{'running':None,'misses':0,'expectedUntil':0,'jobs':[]}}
        self.state.setdefault('invite',{'generation':0,'valid':False,'code':''})
        if not isinstance(self.state['invite'],dict):raise ValueError('Invalid invite state; preserved')
        self.can_send_invites=False
        self.filter_log_noise()

    def filter_log_noise(self):
        kept=[];removed=0
        for item in self.state['pending']:
            text=re.sub(r'^\[THC observer #[0-9]+\]\s*','',item.get('content',''))
            log_alert=item.get('kind')=='log_error' or bool(re.match(r'^(?:(?:Game|API) log:|🚨 \*\*(?:(?:Critical )?(?:Game|API|Server) error)\*\*)',text,re.I))
            if log_alert and not crash_evidence(text):removed+=1
            else:kept.append(item)
        self.state['pending']=kept
        self.state['groups']={key:group for key,group in self.state['groups'].items() if crash_evidence(group.get('text',''))}
        self.state['filteredLogAlerts']=self.state.get('filteredLogAlerts',0)+removed

    def invite_generation(self, log_identity=None):
        previous=self.state['invite']
        self.state['invite']={'generation':previous.get('generation',0)+1,'valid':False,'code':'','catchup':True,'logIdentity':log_identity if log_identity is not None else previous.get('logIdentity','')}
        # Unsent invite codes from earlier sessions must never be announced later.
        self.state['pending']=[item for item in self.state['pending'] if item.get('kind')!='invite']

    def invite_line(self, line, now, historical=False):
        if re.search(r'(?:^|\])\s*Log file open[, :]',line):
            if not historical:self.invite_generation()
            return
        # Only online/session engine categories; echoed chat text is not authority.
        if not re.match(r'^(?:\[[^\]]*\])*\s*(?:LogNetSessionSettings|LogOnlineSession|LogOnline|LogEOSSession):',line):return
        candidates=[]
        if '["JoinCode"]' in line:
            candidates=re.findall(r'\bvalue\[([A-Za-z0-9]{4}-[A-Za-z0-9]{4})\]',line)
        candidates+=re.findall(r'"JoinCode"\s*:\s*"([A-Za-z0-9]{4}-[A-Za-z0-9]{4})"',line)
        codes=set(candidates)
        identity=self.state['process'].get('identity','')
        if len(codes)!=1 or not identity or self.state['process'].get('running') is not True:return
        if historical:
            born=self.state['process'].get('startedAt')
            stamp=re.match(r'^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]',line)
            if type(born) not in (int,float) or not stamp:return
            try:at=datetime.strptime(stamp.group(1),'%Y.%m.%d-%H.%M.%S:%f').replace(tzinfo=timezone.utc).timestamp()
            except ValueError:return
            if at<born-2 or at>now+30:return
        code=codes.pop();invite=self.state['invite']
        if invite.get('valid') and invite.get('code')==code and invite.get('processIdentity')==identity:return
        self.state['pending']=[item for item in self.state['pending'] if item.get('kind')!='invite']
        self.queue('🔑 **Server invite code**\n`'+code+'`',now,'status')
        self.state['pending'][-1].update(kind='invite',generation=invite['generation'])
        invite.update(code=code,valid=True,catchup=False,processIdentity=identity,observedAt=now)

    def catchup_invite(self, path, now):
        invite=self.state['invite'];process=self.state['process']
        if not invite.get('catchup') or not process.get('identity') or type(process.get('startedAt')) not in (int,float):return
        # Bounded startup prefix plus tail; only timestamp-proven current-process
        # invite fields are considered. Historical errors are never replayed.
        with path.open('rb') as stream:
            length=path.stat().st_size
            spans=[(0,min(length,8*1024*1024))]
            if length>8*1024*1024:spans.append((max(8*1024*1024,length-256*1024),length))
            for start,end in spans:
                stream.seek(start)
                if start:stream.readline(8192)
                while stream.tell()<end:
                    line=stream.readline(8192)
                    if not line:break
                    if not line.endswith(b'\n'):continue
                    self.invite_line(line.decode('utf-8',errors='replace'),now,historical=True)
        # Later bytes are followed by the normal tail. Avoid rescanning megabytes
        # on every poll when a build does not emit a recognized invite field.
        self.state['invite']['catchup']=False

    def queue(self, content, now, lane='admin', kind=None):
        if len(self.state['pending'])>=1000:raise ValueError('Alert queue full; log cursor retained for retry')
        ident=self.state['next'];self.state['next']+=1
        self.state['pending'].append({'id':ident,'content':content,'lane':lane,'due':now,'attempt':0})
        if kind:self.state['pending'][-1]['kind']=kind

    @staticmethod
    def error_message(source, text, repeats=0):
        label='Critical game error' if source=='Game log' else 'Critical API error' if source=='API log' else 'Critical server error'
        result='🚨 **'+label+'**\n'+text
        if repeats:result+=f'\n↳ Repeated **{repeats}** additional time(s) since the previous alert.'
        return result

    def line(self, source, line, now, secrets=(), capture_invite=True):
        if source=='Admin audit':
            try:record=json.loads(line)
            except ValueError:return
            if not isinstance(record,dict) or record.get('schema')!=1 or not re.fullmatch('[a-f0-9]{32}',str(record.get('id',''))):return
            seen=self.state.setdefault('auditSeen',[])
            if record['id'] in seen:return
            if isinstance(record.get('details'),dict) and record['details'].get('nativeAuditQueued') is True:
                seen.append(record['id']);self.state['auditSeen']=seen[-1000:];return
            actor=record.get('actor','shared administration');action=record.get('action','admin action');outcome=record.get('outcome','recorded')
            # Keep every stage in the private ledger, but post one terminal control
            # result. Native save requests already have their own Discord audit.
            if (str(action).startswith('control:') and outcome=='accepted') or (action=='/api/thc/save' and outcome=='accepted (completion separate)' and not isinstance(actor,dict)):
                seen.append(record['id']);self.state['auditSeen']=seen[-1000:]
                return
            labels={'/api/thc/access/create':'Create access token','/api/thc/access/revoke':'Revoke access token','/api/thc/recheck':'Compatibility check and recovery','/api/kick':'Kick player','/api/ban':'Ban player','/api/unban':'Unban player','/api/give':'Give item','/api/broadcast':'Broadcast','/api/thc/teleport':'Teleport player','/api/thc/console':'Run console command','/api/thc/save':'Save world','/api/thc/settings':'Change settings','/api/thc/map':'Publish shared map','control:update_server':'Update Dragonwilds server','control:update_dashboard':'Update dashboard','control:rollback_dashboard':'Restore previous dashboard','control:start':'Start server','control:stop':'Stop server','control:restart':'Restart server','control:backup':'Create backup','control:restore':'Restore backup','control:update_game_settings':'Change game settings'}
            action=labels.get(action,str(action));details=record.get('details',{})
            access=(' '.join(str(actor.get('label','Unknown credential')).split())+' ('+str(actor.get('role',''))+')') if isinstance(actor,dict) else str(actor)
            text='🛡️ **Admin audit**\n**Action:** '+action+'\n**Access:** '+access
            if isinstance(actor,dict):text+='\n**Credential ID:** '+str(actor.get('id',''))
            text+='\n**Result:** '+str(outcome)
            if isinstance(details,dict):
                for key,value in details.items():
                    if key in ('job','nativeAuditQueued'):continue
                    label={'player':'Player','item':'Item','count':'Quantity','command':'Command','changedFields':'Changed fields','result':'Details'}.get(key,str(key).capitalize())
                    if isinstance(value,list):value=', '.join(str(x) for x in value)
                    text+='\n**'+label+':** '+str(value)
            self.queue(redact(text,secrets),now,'admin')
            seen.append(record['id']);self.state['auditSeen']=seen[-1000:]
            return
        if source=='Game log' and capture_invite:self.invite_line(line,now)
        if not crash_evidence(line):return
        text=redact(line,secrets)
        # Remove Unreal/API time and frame prefixes only from the grouping key.
        key_text=re.sub(r'^(?:\[[^\]]*\])+\s*','',text)
        key=hashlib.sha256((source+'\n'+key_text).encode()).hexdigest()
        group=self.state['groups'].get(key)
        if group is None:
            if len(self.state['groups'])>=500:
                # Never evict an unsent repeat count; release only inactive groups.
                quiet=[(v['last'],k) for k,v in self.state['groups'].items() if not v['count']]
                if quiet:self.state['groups'].pop(min(quiet)[1])
                else:raise ValueError('Repeat tracker full; log cursor retained')
            self.queue(self.error_message(source,text),now,kind='log_error')
            self.state['groups'][key]={'source':source,'text':text,'last':now,'count':0}
        else:group['count']+=1

    def flush_groups(self, now):
        for group in self.state['groups'].values():
            if group['count'] and now-group['last']>=300:
                self.queue(self.error_message(group['source'],group['text'],group['count']),now,kind='log_error')
                group['last']=now;group['count']=0

    def scan(self, source, path, now, secrets=()):
        path=Path(path)
        try:info=path.stat()
        except FileNotFoundError:
            self.state['files'].setdefault(source,{'identity':'missing','offset':0});return
        if not path.is_file():raise ValueError('Configured log is not a regular file')
        identity=f'{info.st_dev}:{info.st_ino}'
        previous=self.state['files'].get(source)
        if previous is None:
            # Install without replaying old errors. Future rotations start at byte zero.
            self.state['files'][source]={'identity':identity,'offset':info.st_size}
            if source=='Game log':
                self.invite_generation(identity);self.catchup_invite(path,now)
            return
        if previous['identity']!=identity:
            if source=='Game log' and self.state['invite'].get('logIdentity')!=identity:self.invite_generation(identity)
            # Drain a renamed file before the new file, when the retained inode exists.
            for candidate in path.parent.glob('*.log'):
                try:old=candidate.stat()
                except FileNotFoundError:continue
                if f'{old.st_dev}:{old.st_ino}'==previous['identity']:
                    self._scan_file(source,candidate,previous,now,secrets,final=True)
                    if previous['offset']<old.st_size:return
                    break
            previous={'identity':identity,'offset':0};self.state['files'][source]=previous
        if info.st_size<previous['offset']:
            previous['offset']=0
            if source=='Game log':self.invite_generation(identity)
        self._scan_file(source,path,previous,now,secrets)
        if source=='Game log':self.catchup_invite(path,now)

    def _scan_file(self, source, path, cursor, now, secrets, final=False):
        with path.open('rb') as stream:
            if cursor['offset'] and cursor.get('boundary'):
                stream.seek(max(0,cursor['offset']-64))
                boundary=stream.read(min(cursor['offset'],64))
                if hashlib.sha256(boundary).hexdigest()!=cursor['boundary']:
                    cursor['offset']=0
                    if source=='Game log' and not final:self.invite_generation(cursor['identity'])
            stream.seek(cursor['offset']);budget=256*1024
            while budget>0:
                start=stream.tell();line=stream.readline(8192)
                if not line:break
                # Leave incomplete ordinary lines for the next poll.
                if not final and not line.endswith(b'\n') and len(line)<8192:break
                if len(line)==8192 and not line.endswith(b'\n'):
                    while line and not line.endswith(b'\n'):
                        tail=stream.readline(8192)
                        if not tail:break
                        line_end=tail.endswith(b'\n');line = line[:8192] if not line_end else line[:8191]+b'\n'
                        if line_end:break
                        if stream.tell()-start>1024*1024:break
                self.line(source,line.decode('utf-8',errors='replace').rstrip(),now,secrets,capture_invite=not final)
                cursor['offset']=stream.tell();budget-=stream.tell()-start
            stream.seek(max(0,cursor['offset']-64))
            cursor['boundary']=hashlib.sha256(stream.read(min(cursor['offset'],64))).hexdigest()

    def process_status(self, status, now):
        process=self.state['process']
        known=set(process['jobs']);jobs=status.get('jobs',[])
        for job in jobs:
            if process['running'] is not None and job.get('id') not in known and job.get('action') in ('stop','restart') and job.get('state') in ('running','complete'):
                process['expectedUntil']=now+180
        process['jobs']=[job['id'] for job in jobs if isinstance(job.get('id'),str)][-100:]
        running=status.get('running')
        if not isinstance(running,bool):return
        identity=status.get('processIdentity','')
        previous_identity=process.get('identity','')
        self.can_send_invites=running is True and bool(identity)
        if identity!=process.get('identity','') or not running:
            if self.state['invite'].get('valid'):self.invite_generation()
        process['identity']=identity
        process['startedAt']=status.get('processStartedAt')
        if running:
            if process['running'] is False or (previous_identity and identity and previous_identity!=identity):process['expectedUntil']=0
            if process['running'] is False:self.queue('🟢 **Server process is running**\nJoinability is not independently confirmed by this check.',now,'status')
            process['running']=True;process['misses']=0
        else:
            process['misses']+=1
            if process['running'] is True and process['misses']>=3:
                if now>=process['expectedUntil']:
                    self.queue('🔴 **Server process stopped**\nThe game process is no longer observed. Check the dashboard and game logs; the cause is not established.',now,'admin')
                process['running']=False

    def status_heartbeat(self, status, now, interval=2700):
        # A summary is a fresh observation, not an outage alarm or a joinability
        # guarantee. Keep one pending summary; never replay a backlog of them.
        if type(interval) not in (int,float) or not 60<=interval<=86400:return
        running=status.get('running');identity=status.get('processIdentity','')
        if not isinstance(running,bool) or (running and not identity):return
        invite=self.state['invite'];code=invite.get('code','')
        ready=running and invite.get('valid') and invite.get('processIdentity')==identity and isinstance(code,str) and re.fullmatch(r'[A-Za-z0-9]{4}-[A-Za-z0-9]{4}',code)
        summary=self.state.setdefault('heartbeat',{})
        if not ready:
            self.state['pending']=[item for item in self.state['pending'] if item.get('kind')!='status_heartbeat']
            summary.clear()
            return
        key=identity+':'+code
        if summary.get('identity')!=key:
            self.state['pending']=[item for item in self.state['pending'] if item.get('kind')!='status_heartbeat']
            summary.update(identity=key,nextAt=now+interval)
            return
        if now<summary.get('nextAt',now+interval):return
        text='🟢 **The High Celestial is ONLINE!**\n🔑 **Server invite code**\n`'+code+'`'
        pending=next((item for item in self.state['pending'] if item.get('kind')=='status_heartbeat'),None)
        if pending:pending['content']=text  # preserve an existing Discord retry deadline
        else:self.queue(text,now,'status','status_heartbeat')
        summary['nextAt']=now+interval

    def deliver(self, urls, now, transport=post):
        self.filter_log_noise()
        # One attempt per independent lane; a dead admin webhook cannot block status.
        for lane in ('admin','status'):
            item=next((x for x in self.state['pending'] if x['lane']==lane and x.get('kind')!='status_heartbeat'),None) or next((x for x in self.state['pending'] if x['lane']==lane),None)
            if not item or item['due']>now or not urls.get(lane):continue
            if item.get('kind') in ('invite','status_heartbeat') and not self.can_send_invites:continue
            text=re.sub(r'^\[THC observer #[0-9]+\]\s*','',item['content'])
            status,retry=transport(urls[lane],{'content':text[:1900],'allowed_mentions':{'parse':[]}})
            self.state['lastHttpStatus']=status
            if 200<=status<300:
                self.state['pending'].remove(item);self.state['sent']+=1;self.state['lastError']=''
            else:
                item['attempt']=min(20,item['attempt']+1)
                delay=max(min(300,2**min(8,item['attempt'])),min(86400,max(1,retry)))
                if status in (401,403,404):delay=max(delay,300)
                item['due']=now+delay;self.state['lastError']='Discord delivery pending retry; verify configured webhook if rejection persists'
        self.save()

    def save(self):atomic(self.path,self.state)


def control_status(path):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as client:
        client.settimeout(3);client.connect(str(path));client.sendall(b'{"action":"status"}\n')
        with client.makefile('rb') as stream:line=stream.readline(1024*1024)
    result=json.loads(line)
    if result.get('ok') is not True:raise ValueError('Controller status unavailable')
    return result


def configuration(path):
    config=json.loads(Path(path).read_text());root=Path(config['server_root']).resolve()
    def inside(value):
        value=Path(value);value=(root/value).resolve() if not value.is_absolute() else value.resolve()
        if value==root or root not in value.parents:raise ValueError('Log/API path must be inside server root')
        return value
    api=inside(config['api_config']);parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str
    if api.stat().st_size>128*1024:raise ValueError('API configuration too large')
    parser.read(api)
    urls={}
    for lane,key,thread in [('admin','AdminWebhookUrl','AdminThreadId'),('status','StatusWebhookUrl','StatusThreadId')]:
        # Keep failures independent. A bad status lane never disables admin errors.
        try:urls[lane]=route(parser.get('Discord',key,fallback=''),parser.get('Discord',thread,fallback=''))
        except ValueError:urls[lane]=''
    secrets=[value for section in parser for key,value in parser[section].items() if re.search('password|token|webhook',key,re.I)]
    if config.get('settings_file'):
        game_ini=inside(config['settings_file'])
        if game_ini.is_file() and game_ini.stat().st_size<=128*1024:
            settings=configparser.ConfigParser(interpolation=None,strict=False)
            try:
                settings.read(game_ini)
                secrets.extend(value.strip('"\'') for section in settings for key,value in settings[section].items() if re.search('password|token',key,re.I))
            except configparser.Error:pass
    game=inside(config['log_file']) if config.get('log_file') else None
    logs={'API log':api.parent/'logs/api.log','Admin audit':api.parent/'thc/controller-audit.log'}
    if game:logs['Game log']=game
    minutes=config.get('status_interval_minutes',45)
    if type(minutes) not in (int,float) or not 1<=minutes<=1440:raise ValueError('Status interval must be 1 to 1440 minutes')
    return {'urls':urls,'secrets':secrets,'logs':logs,'socket':config['socket'],'state':api.parent/'thc/error-watch.json','statusIntervalSeconds':int(minutes*60)}


def main():
    os.umask(0o077);parser=argparse.ArgumentParser();parser.add_argument('--config',required=True);args=parser.parse_args()
    config=configuration(args.config);watch=Watcher(config['state'])
    print('THC watcher active: admin audits and crash-level alerts only; ordinary errors/warnings excluded. No game control is performed.',flush=True)
    warned=False;problem=False
    while True:
        try:
            config=configuration(args.config);now=int(time.time())
            watch.can_send_invites=False
            observed=None
            try:
                observed=control_status(config['socket']);watch.process_status(observed,now)
            except (OSError,ValueError):pass
            for source,path in config['logs'].items():watch.scan(source,path,now,config['secrets'])
            if observed is not None:watch.status_heartbeat(observed,now,config['statusIntervalSeconds'])
            watch.flush_groups(now);watch.save();watch.deliver(config['urls'],now)
            if not config['urls'].get('admin') and not warned:
                print('THC error watcher: admin webhook is missing/invalid; pending alerts retained.',flush=True);warned=True
            if config['urls'].get('admin'):warned=False
            problem=False
        except (OSError,ValueError,KeyError,TypeError):
            if not problem:print('THC error watcher: state/configuration error; inspect local files. No restart or game action performed.',flush=True)
            problem=True
        time.sleep(5)

if __name__=='__main__':main()
