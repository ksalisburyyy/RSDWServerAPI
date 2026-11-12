#!/usr/bin/env python3
"""Optional external observer. No game control, restarts, SSH or save access."""
import json
import os
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

class Monitor:
    def __init__(self,path):
        self.path=Path(path)
        if self.path.exists():
            self.state=json.loads(self.path.read_text())
            if self.state.get('schema')!=1 or not isinstance(self.state.get('pending'),list):
                raise ValueError('Invalid watcher state; original preserved')
        else:self.state={'schema':1,'online':None,'failures':0,'pending':[],'next':1}
    def save(self):
        self.path.parent.mkdir(parents=True,exist_ok=True)
        fd,name=tempfile.mkstemp(dir=self.path.parent,prefix='.thc-watch-')
        try:
            with os.fdopen(fd,'w') as file:
                json.dump(self.state,file);file.flush();os.fsync(file.fileno())
            os.replace(name,self.path)
        finally:
            if os.path.exists(name):os.unlink(name)
    def observe(self,online,now):
        # None means an authentication/configuration error, not evidence of outage.
        if online is None:return
        previous=self.state['online']
        if online:self.state['failures']=0
        else:
            self.state['failures']+=1
            if self.state['failures']<3:self.save();return
        if online!=previous:
            if online or previous is True:
                if len(self.state['pending'])>=1000:raise RuntimeError('Watcher outbox full')
                label='Game world observed online' if online else 'THC monitoring unavailable; check the game process and API'
                self.state['pending'].append({'id':self.state['next'],'message':label,'time':now,'due':now,'attempt':0})
                self.state['next']+=1
            self.state['online']=online
        self.save()
    def deliver(self,now,post):
        if not self.state['pending'] or self.state['pending'][0]['due']>now:return
        item=self.state['pending'][0]
        status,retry=post({'content':f"[THC watcher #{item['id']}] {item['message']}",'allowed_mentions':{'parse':[]}})
        if 200<=status<300:self.state['pending'].pop(0)
        else:
            item['attempt']=min(item['attempt']+1,20)
            item['due']=now+max(min(2**min(item['attempt'],8),300),min(max(retry,1),86400))
        self.save()

def health(base,token):
    try:
        request=urllib.request.Request(base+'/api/thc/status',headers={'Authorization':'Bearer '+token})
        with urllib.request.urlopen(request,timeout=5) as response:data=json.load(response)
        return data.get('worldObserved') is True
    except urllib.error.HTTPError as error:
        if error.code in (401,403):return None
        return False
    except (OSError,ValueError):return False

def webhook(url,payload):
    try:
        request=urllib.request.Request(url,data=json.dumps(payload).encode(),headers={'Content-Type':'application/json'},method='POST')
        with urllib.request.urlopen(request,timeout=8) as response:return response.status,0
    except urllib.error.HTTPError as error:
        try:retry=int(float(error.headers.get('Retry-After','1')))+1
        except ValueError:retry=1
        return error.code,retry
    except OSError:return 0,1

def main():
    os.umask(0o077)
    base=os.environ.get('THC_API_URL','http://127.0.0.1:8081').rstrip('/')
    parsed=urllib.parse.urlsplit(base)
    if parsed.scheme!='http' or parsed.hostname not in ('127.0.0.1','localhost','::1') or parsed.username or parsed.query or parsed.fragment:
        raise SystemExit('THC_API_URL must name a local HTTP listener')
    token=os.environ.get('THC_API_TOKEN','');url=os.environ.get('THC_STATUS_WEBHOOK','')
    parsed_webhook=urllib.parse.urlsplit(url)
    if not token or parsed_webhook.scheme!='https' or parsed_webhook.hostname not in ('discord.com','discordapp.com') or not parsed_webhook.path.startswith('/api/webhooks/'):
        raise SystemExit('Configure API token and a Discord HTTPS webhook first')
    monitor=Monitor(os.environ.get('THC_WATCH_STATE','thc-watcher-state.json'))
    while True:
        observed=health(base,token)
        if observed is None:print('THC watcher: authentication rejected; check token',flush=True)
        try:
            monitor.observe(observed,int(time.time()));monitor.deliver(int(time.time()),lambda data:webhook(url,data))
        except (OSError,RuntimeError,ValueError):print('THC watcher: state or delivery error; original files retained where possible',flush=True)
        time.sleep(5)
if __name__=='__main__':main()
