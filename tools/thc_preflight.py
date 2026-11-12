#!/usr/bin/env python3
"""Read-only check of a THC control configuration. No writes, signals or launches."""
import argparse
import configparser
import json
import os
import re
import signal
from pathlib import Path

def inspect(config):
    root=Path(config['server_root']).resolve()
    result={'uid':os.geteuid(),'mode':config.get('mode','disabled'),'adoptExisting':config.get('adopt_existing',False),'checks':{},'ready':True}
    def check(name,ok,detail):
        result['checks'][name]={'ok':bool(ok),'detail':str(detail)}
        if not ok:result['ready']=False
    check('serverRoot',root.is_dir(),root)
    def inside(key):
        path=Path(config.get(key,''));path=(root/path).resolve() if not path.is_absolute() else path.resolve()
        valid=path!=root and root in path.parents
        check(key,valid and path.exists(),path)
        return path if valid else None
    for key in ['save_directory','game_executable']:inside(key)
    for key in ['log_file','settings_file']:
        if config.get(key):inside(key)
        else:check(key,True,'not configured; related panel disabled')
    api=inside('api_config')
    if api and api.is_file():
        try:
            parser=configparser.ConfigParser(interpolation=None);parser.read(api)
            check('ownerToken',bool(parser.get('API','BearerToken',fallback='')),'configured' if parser.get('API','BearerToken',fallback='') else 'missing')
            check('apiPort',1<=parser.getint('API','Port',fallback=8081)<=65535,parser.getint('API','Port',fallback=8081))
        except (ValueError,configparser.Error):check('apiConfiguration',False,'could not parse')
    dashboard=Path(config.get('dashboard_file','')).resolve();check('dashboard',dashboard.is_file(),dashboard)
    backups=Path(config['backup_directory']).resolve();saves=inside('save_directory')
    check('backupSeparation',saves is not None and saves!=backups and saves not in backups.parents and backups not in saves.parents,backups)
    socket=str(config.get('socket',''));expected=(api.parent/'thc/control.sock') if api else None
    check('socket',bool(expected) and Path(socket).resolve()==expected and len(os.fsencode(socket))<108,'matches mod data directory' if expected and Path(socket).resolve()==expected else 'must equal <API config directory>/thc/control.sock')
    check('localhost',config.get('http_bind','127.0.0.1')=='127.0.0.1','127.0.0.1 required')
    mode=config.get('mode','disabled');check('mode',mode in {'owned_process','systemd_user'},mode+' (disabled requires configuration)' if mode=='disabled' else mode)
    if mode=='owned_process':
        command=config.get('command',[]);check('command',isinstance(command,list) and bool(command) and all(isinstance(x,str) for x in command),'fixed argv configured' if command else 'missing')
        if len(command)>1 and command[0] in ['/bin/bash','/bin/sh']:check('startupScript',Path(command[1]).is_file(),'startup script exists' if Path(command[1]).is_file() else 'startup script missing')
    if mode=='systemd_user':check('gameUnit',bool(re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.@-]*\.service',str(config.get('service_unit','')))) and config['service_unit'] not in {'thc-dashboard.service','thc-discord.service','thc-control.service'},config.get('service_unit','missing'))
    if config.get('adopt_existing'):check('pidfd',mode=='owned_process' and hasattr(os,'pidfd_open') and hasattr(signal,'pidfd_send_signal'),'Linux pidfd required for existing-process control')
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--config',required=True);args=parser.parse_args()
    try:result=inspect(json.loads(Path(args.config).read_text()))
    except (OSError,ValueError,KeyError,TypeError):raise SystemExit('Could not read a valid control configuration; no files changed')
    print(json.dumps(result,indent=2));return 0 if result['ready'] else 2
if __name__=='__main__':raise SystemExit(main())
