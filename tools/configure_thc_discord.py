#!/usr/bin/env python3
"""Apply the owner's five thread destinations without printing webhook credentials."""
import argparse
import configparser
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
import urllib.parse
from thc_error_watch import route, post

DESTINATIONS = [
    ('chat', 'WebhookUrl', 'ChatThreadId', '1560961724671332372'),
    ('kills/deaths', 'DeathWebhookUrl', 'DeathThreadId', '1560962632582959195'),
    ('player activity', 'PresenceWebhookUrl', 'PresenceThreadId', '1560963104303489184'),
    ('server status', 'StatusWebhookUrl', 'StatusThreadId', '1560963614628909086'),
    ('admin logs', 'AdminWebhookUrl', 'AdminThreadId', '1560963849358934087'),
]


def rewrite(text, updates):
    lines=text.splitlines(keepends=True);result=[];section='';written=set();found=False
    def missing():
        for key,value in updates.items():
            if key not in written:result.append(key+'='+value+'\n');written.add(key)
    for line in lines:
        match=re.fullmatch(r'\s*\[([^\]]+)\]\s*(?:[;#].*)?',line.strip())
        if match:
            if section=='Discord':missing()
            section=match.group(1)
            if section=='Discord':found=True
        if section=='Discord':
            pair=re.match(r'\s*([^=;#]+?)\s*=',line)
            if pair and pair.group(1) in updates:
                key=pair.group(1)
                if key not in written:result.append(key+'='+updates[key]+'\n');written.add(key)
                continue
        result.append(line if line.endswith('\n') else line+'\n')
    if section=='Discord':missing()
    if not found:result.append('\n[Discord]\n');missing()
    return ''.join(result)


def prepare(config_file, private_file):
    config=json.loads(Path(config_file).read_text());root=Path(config['server_root']).resolve()
    api=Path(config['api_config']);api=(root/api).resolve() if not api.is_absolute() else api.resolve()
    if root not in api.parents:raise ValueError('API settings must be inside server root')
    before=api.read_text()
    if len(before)>128*1024:raise ValueError('API settings exceed size limit')
    parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str;parser.read_string(before)
    if not parser.get('API','BearerToken',fallback=''):raise ValueError('Owner API token is missing')
    private={}
    if Path(private_file).exists():
        if Path(private_file).stat().st_size>16384:raise ValueError('Private webhook file exceeds size limit')
        private=json.loads(Path(private_file).read_text())
    chat=parser.get('Discord','WebhookUrl',fallback='') or private.get('WebhookUrl','')
    admin=parser.get('Discord','AdminWebhookUrl',fallback='') or private.get('AdminWebhookUrl','')
    updates={'Enabled':'true','Username':''};summary=[]
    for label,key,thread_key,thread in DESTINATIONS:
        url=private.get(key,'') or parser.get('Discord',key,fallback='') or (admin if key=='AdminWebhookUrl' else chat)
        if not isinstance(url,str) or not url:raise ValueError('Missing '+label+' webhook; no settings changed')
        route(url,thread) # Validate the destination without making Discord calls.
        parsed=urllib.parse.urlsplit(url)
        # Store thread routing explicitly; retain unrelated query options.
        query=urllib.parse.urlencode([(k,v) for k,v in urllib.parse.parse_qsl(parsed.query,keep_blank_values=True) if k!='thread_id'])
        base=urllib.parse.urlunsplit((parsed.scheme,parsed.netloc,parsed.path,query,''))
        updates[key]=base;updates[thread_key]=thread
        summary.append({'lane':label,'threadId':thread,'webhookConfigured':True})
    return api,before,rewrite(before,updates),summary


def test_routes(text, transport=post):
    parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str;parser.read_string(text)
    results=[]
    for label,key,thread_key,thread in DESTINATIONS:
        url=route(parser.get('Discord',key),parser.get('Discord',thread_key))
        status,retry=transport(url,{'content':'🧪 **THC routing test** · '+label,'allowed_mentions':{'parse':[]}})
        results.append({'lane':label,'threadId':thread,'httpStatus':status,'deliveryConfirmed':200<=status<300,'retryAfterSeconds':retry})
    return results


def main():
    args=argparse.ArgumentParser();args.add_argument('--config',default=str(Path.home()/'.config/thc-control.json'));args.add_argument('--private',default=str(Path(__file__).with_name('discord-webhooks.private.json')));mode=args.add_mutually_exclusive_group();mode.add_argument('--dry-run',action='store_true');mode.add_argument('--test-routes',action='store_true');options=args.parse_args()
    if os.geteuid()==0:raise SystemExit('Run this as thc, not root. It only configures the THC notification routes.')
    try:
        api,before,after,summary=prepare(options.config,options.private)
        if options.test_routes:
            print(json.dumps({'testMessagesSent':True,'results':test_routes(after)},indent=2));return
        backup=None
        if not options.dry_run:
            # Refuse to overwrite a concurrent dashboard settings update.
            if api.read_text()!=before:raise ValueError('Settings changed during setup; retry')
            backup=api.with_name(api.name+'.before-discord-'+str(time.time_ns()))
            shutil.copy2(api,backup);os.chmod(backup,0o600)
            fd,temporary=tempfile.mkstemp(dir=api.parent,prefix='.thc-discord-')
            try:
                with os.fdopen(fd,'w') as stream:stream.write(after);stream.flush();os.fsync(stream.fileno())
                if api.read_text()!=before:raise ValueError('Settings changed during setup; retry')
                os.replace(temporary,api)
            finally:
                if os.path.exists(temporary):os.unlink(temporary)
        print(json.dumps({'dryRun':options.dry_run,'destinations':summary,'nativeGameRestartRequired':True,'backup':str(backup) if backup else None},indent=2))
    except (OSError,ValueError,KeyError,TypeError,configparser.Error) as error:
        # Error details must never include a URL/token from a parser/HTTP exception.
        raise SystemExit('Discord setup could not complete; no credentials printed. Check private webhook input and controller/API paths.')

if __name__=='__main__':main()
