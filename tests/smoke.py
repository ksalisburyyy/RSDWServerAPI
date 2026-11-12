#!/usr/bin/env python3
"""Exercise the built .so on loopback in a non-game fixture, never on a VPS."""
import http.client
import importlib.util
import threading
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

with tempfile.TemporaryDirectory(prefix='thc-smoke-') as folder:
    path = Path(folder)
    api_port, rcon_port = free_port(), free_port()
    while rcon_port == api_port:
        rcon_port = free_port()
    (path / 'settings.ini').write_text(f'''[General]
EnableLogging=true
VerboseLogging=false
[API]
Enabled=true
BindAddress=127.0.0.1
Port={api_port}
BearerToken=test-only-token
[RCON]
Enabled=true
BindAddress=127.0.0.1
Port={rcon_port}
Password=test-only-password
[Discord]
Enabled=false
''')
    (path/'thc').mkdir()
    fixture_events=[{'id':i,'type':'fixture','guid':'','name':'Fixture','time':1000,'details':{}} for i in range(101,1101)]
    (path/'thc/state.json').write_text(json.dumps({'schema':1,'nextEvent':1101,'characters':{},'events':fixture_events}))
    seed = '76561190000000000'
    (path / 'bans.json').write_text(json.dumps([
        dict(netId=seed, name='Fixture', reason='test', bannedAt='2026-10-25T00:00:00Z')]))
    env = os.environ.copy()
    env.update(LD_PRELOAD=str(ROOT / 'dist/librsdwapi.so'), RSDWAPI_DIR=folder)
    log = open(path / 'host.log', 'w+')
    process = subprocess.Popen([str(ROOT / 'build/tests/preload_host')],
                               stdin=subprocess.PIPE, stdout=log, stderr=log, env=env)
    checks = 0
    def request(method, endpoint, body=None, token='test-only-token'):
        conn = http.client.HTTPConnection('127.0.0.1', api_port, timeout=3)
        headers = {} if token is None else {'Authorization': 'Bearer ' + token}
        if body is not None:
            body = json.dumps(body)
            headers['Content-Type'] = 'application/json'
        conn.request(method, endpoint, body=body, headers=headers)
        response = conn.getresponse()
        result = response.read()
        status = response.status
        conn.close()
        return status, json.loads(result) if result else None
    def expect(method, endpoint, status, body=None, token='test-only-token'):
        global checks
        actual, data = request(method, endpoint, body, token)
        assert actual == status, (endpoint, actual, data)
        checks += 1
        return data
    def read_exact(s, length):
        data = b''
        while len(data) < length:
            chunk = s.recv(length - len(data))
            assert chunk, 'RCON disconnected'
            data += chunk
        return data
    def receive(s):
        size, = struct.unpack('<i', read_exact(s, 4))
        assert 10 <= size <= 8192
        data = read_exact(s, size)
        ident, kind = struct.unpack('<ii', data[:8])
        return ident, kind, data[8:-2].decode()
    def send(s, ident, kind, body):
        packet = struct.pack('<ii', ident, kind) + body.encode() + b'\0\0'
        s.sendall(struct.pack('<i', len(packet)) + packet)
    try:
        for _ in range(100):
            if process.poll() is not None:
                raise AssertionError('preloaded host exited early')
            try:
                health = expect('GET', '/api/health', 200)
                break
            except (OSError, http.client.HTTPException):
                time.sleep(.05)
        else:
            raise AssertionError('local fixture API did not start')
        assert health['version'] == '0.5.0-thc.8'
        recent=expect('GET','/api/thc/events?latest=1&limit=500',200)
        assert len(recent['events'])==500 and recent['events'][-1]['id']==recent['latestId']
        assert recent['events'][0]['id']==recent['latestId']-499
        original=expect('GET','/api/thc/events?after=600&limit=10',200)
        assert original['events'][0]['id']==601 and original['nextAfter']==610
        expect('GET','/api/thc/events?latest=1&after=1',400)
        expect('GET','/api/thc/events?latest=invalid',400)
        assert health['engineReady'] is False
        assert health['broadcastReady'] is False
        assert health['chatHooked'] is False
        assert health['nativeCallsReady'] is False
        expect('GET', '/api/hello', 200)
        assert expect('GET','/api/thc/access/me',200)['role']=='admin'
        viewer=expect('POST','/api/thc/access/create',200,{'label':'smoke viewer','role':'viewer'})
        assert 'token' in viewer
        identity=expect('GET','/api/thc/access/me',200,token=viewer['token'])
        assert identity['label']=='smoke viewer' and identity['id']==viewer['id'] and 'token' not in identity
        expect('GET','/api/health',200,token=viewer['token'])
        expect('GET','/api/thc/settings',403,token=viewer['token'])
        expect('POST','/api/kick',403,{'player':seed},token=viewer['token'])
        expect('POST','/api/thc/recheck',403,{},token=viewer['token'])
        expect('POST','/api/thc/access/revoke',200,{'id':viewer['id']})
        expect('GET','/api/health',401,token=viewer['token'])
        settings=expect('GET','/api/thc/settings',200)
        assert 'bearerToken' not in settings and 'chatWebhook' not in settings
        assert len(expect('GET','/api/thc/analytics',200)['hours'])==24
        expect('POST','/api/thc/save',503,{})
        expect('POST','/api/thc/recheck',503,{})
        expect('POST','/api/thc/teleport',503,{'player':seed,'x':10,'y':20,'z':30})
        expect('POST','/api/thc/console',503,{'command':'stat fps'})
        expect('POST','/api/thc/control',409,{'action':'restart'})

        assert expect('GET','/api/thc/status',200)['ledger']['loaded'] is True
        assert expect('GET','/api/thc/save',200)['available'] is False
        expect('POST','/api/thc/settings',400,{'adminThreadId':1560963849358934087})
        expect('POST','/api/thc/settings',400,{'adminThreadId':'18446744073709551616'})
        expect('POST','/api/thc/settings',200,{'adminThreadId':'1560963849358934087'})
        assert expect('GET','/api/thc/settings',200)['threadIds']['admin']=='1560963849358934087'
        assert expect('GET','/api/thc/characters',200)['characters'] == []
        assert any(e['type']=='starting' for e in expect('GET','/api/thc/events?latest=1&limit=500',200)['events'])
        expect('GET','/api/thc/events?after=invalid',400)
        expect('GET','/api/thc/events?limit=999999',400)
        conn=http.client.HTTPConnection('127.0.0.1',api_port,timeout=3)
        conn.request('GET','/dashboard')
        response=conn.getresponse()
        assert response.status==200 and b'THC | Dragonwilds' in response.read()
        conn.close(); checks += 1
        with socket.create_connection(('127.0.0.1',api_port),timeout=3) as fragmented:
            fragmented.sendall(b'GET /api/health HTTP/1.1\r\nHost: localhost\r\n')
            time.sleep(.02)
            fragmented.sendall(b'authorization: Bearer test-only-token\r\n\r\n')
            reply=b''
            while True:
                chunk=fragmented.recv(8192)
                if not chunk: break
                reply+=chunk
            assert reply.startswith(b'HTTP/1.1 200'),reply
            checks += 1
        for raw in [
            b'POST /api/unban HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\n\r\n',
            b'POST /api/unban HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100000000\r\n\r\n',
            b'POST /api/unban HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer test-only-token\r\nContent-Length: 5\r\n\r\nbroke']:
            with socket.create_connection(('127.0.0.1',api_port),timeout=3) as bad:
                bad.sendall(raw)
                assert bad.recv(8192).startswith(b'HTTP/1.1 400')
                checks += 1

        expect('GET', '/api/health', 401, token=None)
        expect('GET', '/api/players', 401, token='wrong')
        expect('GET', '/api/unknown', 404)
        for endpoint in ('/api/players', '/api/items'):
            expect('GET', endpoint, 503)
        for endpoint, body in (
            ('/api/kick', {'player': seed}), ('/api/give', {'player': seed, 'item': 'IronBar'}),
            ('/api/ban', {'player': seed}), ('/api/broadcast', {'message': 'test'})):
            expect('POST', endpoint, 503, body)
        assert expect('GET', '/api/bans', 200)['bans'][0]['netId'] == seed
        expect('POST', '/api/unban', 400, {})
        expect('POST', '/api/unban', 200, {'player': seed})
        assert expect('GET', '/api/bans', 200)['bans'] == []
        assert json.loads((path / 'bans.json').read_text()) == []
        expect('POST', '/api/unban', 404, {'player': seed})
        with socket.create_connection(('127.0.0.1', rcon_port), timeout=3) as s:
            send(s, 1, 3, 'wrong')
            assert receive(s) == (1, 0, '')
            assert receive(s) == (-1, 2, '')
            checks += 1
        with socket.create_connection(('127.0.0.1', rcon_port), timeout=3) as s:
            send(s, 2, 3, 'test-only-password')
            assert receive(s) == (2, 0, '')
            assert receive(s) == (2, 2, '')
            for index, (command, contains) in enumerate([
                ('help', 'broadcast'), ('health', 'status=starting'),
                ('players', 'Engine not initialized'), ('bans', 'No bans.'),
                ('does-not-exist', 'Unknown command')], 3):
                send(s, index, 2, command)
                ident, kind, text = receive(s)
                assert (ident, kind) == (index, 0)
                assert contains in text, text
                checks += 1
        # Exercise the compiled Unix gateway against the real control service,
        # using only an offline temporary save directory.
        spec=importlib.util.spec_from_file_location('fixture_control',ROOT/'tools/thc_control.py')
        control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
        (path/'savefiles').mkdir();(path/'savefiles/world.sav').write_bytes(b'smoke-save-fixture')
        controller=control.Controller({'server_root':folder,'save_directory':'savefiles','backup_directory':folder+'-backups','mode':'disabled'})
        control_server=control.Server(str(path/'thc/control.sock'),control.Handler);control_server.controller=controller
        control_thread=threading.Thread(target=control_server.serve_forever,daemon=True);control_thread.start()
        try:
            assert expect('GET','/api/thc/control',200)['ok'] is True
            assert expect('GET','/api/thc/control/backups',200)['backups']==[]
            job=expect('POST','/api/thc/control',200,{'action':'backup'})
            for _ in range(40):
                status=expect('GET','/api/thc/control',200)
                result=next(row for row in status['jobs'] if row['id']==job['job'])
                if result['state']!='running':break
                time.sleep(.05)
            assert result['state']=='complete',result
            assert len(expect('GET','/api/thc/control/backups',200)['backups'])==1
        finally:
            control_server.shutdown();control_server.server_close();control_thread.join()
            import shutil
            shutil.rmtree(folder+'-backups')
        process.stdin.write(b'x')
        process.stdin.flush()
        assert process.wait(timeout=8) == 0
        checks += 1
        print(f'PASS: {checks} built-library loader/REST/RCON/persistence/shutdown checks (loopback only, no game)')
    except BaseException:
        log.flush()
        log.seek(0)
        print(log.read())
        raise
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        log.close()
