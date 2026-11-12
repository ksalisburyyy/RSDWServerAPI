#!/usr/bin/env python3
"""Browser checks against the embedded dashboard and fake API responses only."""
import json
import base64
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from playwright.sync_api import sync_playwright

root=Path(__file__).resolve().parents[1]
html=(root/'src/api/dashboard.h').read_text().split('R"THCHTML(',1)[1].rsplit(')THCHTML"',1)[0]
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        banner=urlparse(self.path).path=='/assets/thc-banner.png'
        data=(root/'assets/thc-banner.png').read_bytes() if banner else html.encode();self.send_response(200);self.send_header('Content-Type','image/png' if banner else 'text/html; charset=utf-8');self.end_headers();self.wfile.write(data)
    def log_message(self,*args): pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
malicious='<img src=x onerror="window.bad=true">'
player={'name':'Platform','characterName':malicious,'uniqueNetId':'76561190000000000','characterGuid':'A'*32,'playerId':1,'platform':'PC','location':{'x':100,'y':200,'z':300},'pingMs':40,'health':{'current':90,'max':100},'sustenance':{'current':80,'max':100},'hydration':{'current':70,'max':100}}
requests=[];errors=[];request_urls=[]
save_fixture={'available':True,'busy':False,'request':{'state':'idle'}}
control_fixture={'ok':True,'mode':'owned_process','running':True,'restartConfigured':True,'settingsConfigured':True,'jobs':[]}
try:
 with sync_playwright() as pw:
    browser=pw.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
    page=browser.new_page(viewport={'width':1440,'height':1000});page.on('dialog',lambda dialog:dialog.accept());page.on('pageerror',lambda error: errors.append(str(error)))
    def intercept(route):
        request=route.request;path=urlparse(request.url).path
        if not path.startswith('/api/'):
            if request.url.startswith(f'http://127.0.0.1:{server.server_port}/'):route.continue_()
            else:route.abort()
            return
        if request.headers.get('authorization') not in ('Bearer fixture','Bearer viewer'):route.fulfill(status=401,json={'error':'Unauthorized'});return
        payload=request.post_data_json if request.method=='POST' else None
        if request.headers.get('authorization')=='Bearer viewer' and request.method=='POST':route.fulfill(status=403,json={'error':'Read-only access'});return
        requests.append((path,payload));request_urls.append(request.url)
        data={
            '/api/thc/access/me':{'role':'viewer' if request.headers.get('authorization')=='Bearer viewer' else 'admin','label':'Viewer' if request.headers.get('authorization')=='Bearer viewer' else 'Minghoo','id':'fixture-id'},
            '/api/thc/access':{'tokens':[{'id':'token1','label':'Viewer fixture','role':'viewer'}]},
            '/api/thc/access/create':{'id':'token2','label':'Test','role':'viewer','token':'one-time-fixture-secret'},
            '/api/thc/settings':{'apiPort':8081,'combatTracking':True,'saveCommand':'','discordEnabled':True,'discordUsername':'THC','threadIds':{'chat':'1560961724671332372'},'webhooks':{'chat':True}},
            '/api/thc/control':control_fixture,
            '/api/thc/control/logs':{'ok':True,'lines':['[fixture] real log transport']},
            '/api/thc/control/backups':{'ok':True,'backups':[{'id':'20261025000000-1234abcd','time':1000,'bytes':1024,'files':1}]},
            '/api/thc/control/game-settings':{'ok':True,'settings':{'ServerName':'THC fixture','MaxPlayers':'4','WorldPassword':True}},
            '/api/thc/recheck':{'id':1,'state':'queued','message':'Validated discovery retry queued'},
            '/api/thc/analytics':{'hours':[{'time':1000+i*3600,'joins':i%3,'deaths':i%2,'kills':i%4,'chat':i%5} for i in range(24)]},
            '/api/thc/invite':{'available':True,'code':'ABCD-1234'},
            '/api/health':{'inviteCodeAvailable':True,'version':'test','uptimeSeconds':600,'engineReady':True,'nativeCallsReady':True,'chatHooked':True,'broadcastReady':True},
            '/api/players':{'count':1,'players':[player]},'/api/bans':{'bans':[]},
            '/api/thc/characters':{'count':1,'characters':[{'guid':'A'*32,'name':'Renamed','online':True,'kills':3,'deaths':1,'joins':2,'secondsPlayed':3600}]},
            '/api/thc/events':{'events':[{'id':1,'type':'join','name':malicious,'time':1000,'details':{}}]},
            '/api/thc/status':{'worldSave':save_fixture,'worldObserved':True,'ledger':{'loaded':True},'discord':{},'administration':{'teleport':True,'console':True,'saveWorld':True,'saveWorldNative':True},'system':{'cpuPercent':12.5,'rssBytes':1073741824,'memoryTotalBytes':8589934592,'diskUsedBytes':400,'diskTotalBytes':1000,'processUptimeSeconds':3600}},
            '/api/items':{'items':[{'name':'ITEM_Resources_IronBar','maxStackSize':50}]}}
        if path=='/api/thc/save' and request.method=='POST':
            save_fixture.update({'busy':True,'request':{'state':'pending'}});route.fulfill(status=202,json={'status':'accepted','job':42,'message':'World save requested; awaiting completion'});return
        if path=='/api/broadcast':route.fulfill(status=500,json={'error':'Broadcast waiting for game function'});return
        route.fulfill(json=data.get(path,{'status':'ok','message':'Fixture action completed'}))
    page.route('**/*',intercept)
    page.goto(f'http://127.0.0.1:{server.server_port}/dashboard')
    page.locator('#token').fill('wrong');page.get_by_role('button',name='Connect',exact=True).click();page.get_by_role('status').filter(has_text='Unauthorized').wait_for()
    assert page.locator('#login').is_visible()
    page.locator('#token').fill('fixture');page.get_by_role('button',name='Connect',exact=True).click();page.locator('#app').wait_for(state='visible')
    page.locator('#playerCount').filter(has_text='1').wait_for()
    page.locator('#inviteCode').filter(has_text='ABCD-1234').wait_for();assert page.locator('#inviteCode').is_enabled()
    assert page.locator('#versionBadge + #inviteCode').count()==1
    page.evaluate("Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async(value)=>{window.copiedInvite=value}}})")
    page.locator('#inviteCode').click();assert page.evaluate('window.copiedInvite')=='ABCD-1234'

    page.evaluate("renderInvite({available:false})");assert page.locator('#inviteCode').is_disabled() and page.locator('#inviteCode').inner_text()=='Invite: waiting'
    page.evaluate('refresh()');page.locator('#inviteCode').filter(has_text='ABCD-1234').wait_for()
    page.get_by_role('button',name='Players',exact=True).click();page.locator('#playersTable').get_by_text(malicious,exact=True).click()
    assert json.loads(page.locator('#playerDetail').inner_text())['characterName']==malicious
    assert page.locator('#playersTable img').count()==0 and not page.evaluate('Boolean(window.bad)')
    page.get_by_role('button',name='Administration',exact=True).click();page.locator('#message').fill('Hello');page.get_by_role('button',name='Send broadcast').click();page.locator('#result').filter(has_text='Broadcast waiting').wait_for()
    page.get_by_role('button',name='Items',exact=True).click();page.locator('#search').fill('iron');page.get_by_role('button',name='Search',exact=True).click();page.locator('#itemsTable').get_by_text('ITEM_Resources_IronBar',exact=True).click();page.locator('#count').fill('2');page.get_by_role('button',name='Give item',exact=True).click();page.locator('#result').filter(has_text='Fixture action completed').wait_for()
    assert any(path=='/api/give' and body['count']==2 and body['player']==player['uniqueNetId'] for path,body in requests)
    page.get_by_role('button',name='Character records',exact=True).click();page.locator('#historyTable').get_by_text('Renamed',exact=True).click();assert '"kills": 3' in page.locator('#historyDetail').inner_text()
    page.get_by_role('button',name='World Map',exact=True).click()
    for key,value in [('mapMinX','-1000'),('mapMaxX','1000'),('mapMinY','-1000'),('mapMaxY','1000')]:page.locator('#'+key).fill(value)
    page.get_by_role('button',name='Apply coordinate bounds').click()
    page.locator('#teleportX').fill('10');page.locator('#teleportY').fill('20');page.locator('#teleportZ').fill('30')
    page.get_by_role('button',name='Teleport',exact=True).click();page.locator('#result').filter(has_text='Fixture action completed').wait_for()
    assert any(path=='/api/thc/teleport' and body['z']==30 for path,body in requests)
    page.get_by_role('button',name='Server Console',exact=True).click();page.locator('#gameLog').filter(has_text='real log transport').wait_for();page.locator('#consoleCommand').fill('stat fps');page.get_by_role('button',name='Run Command',exact=True).click()
    page.get_by_role('button',name='Bans & Permissions',exact=True).click();page.locator('#accessLabel').fill('Test');page.get_by_role('button',name='Create token',exact=True).click();page.locator('#createdTokenWrap').wait_for(state='visible');assert page.locator('#createdToken').input_value()=='one-time-fixture-secret'
    page.get_by_role('button',name='Discord Integration',exact=True).click();page.wait_for_function("document.querySelector('#discordUsername').value === 'THC'");page.locator('#discordUsername').fill('THC Test');page.get_by_role('button',name='Fill unconfigured THC thread destinations',exact=True).click();assert page.locator('#chatThreadId').input_value()=='1560961724671332372';assert page.locator('#adminThreadId').input_value()=='1560963849358934087';page.get_by_role('button',name='Save Discord settings',exact=True).click();page.wait_for_timeout(300)
    assert any(path=='/api/thc/settings' and body is not None and body.get('discordUsername')=='THC Test' for path,body in requests)
    assert any('/api/thc/events?latest=1&limit=500' in url for url in request_urls)
    page.get_by_role('button',name='Server Management',exact=True).click()
    assert page.locator('#compatibilityTable').get_by_text('Engine discovery',exact=True).is_visible()
    assert page.locator('#recoverIntegrations').is_enabled()
    page.locator('#recoverIntegrations').click();page.wait_for_timeout(200)
    assert any(path=='/api/thc/recheck' and payload=={} for path,payload in requests)
    report=page.evaluate('compatibilitySnapshot');assert report['health']['version']=='test' and 'token' not in report
    with page.expect_download() as download:page.locator('#downloadCompatibility').click()
    assert download.value.suggested_filename.startswith('THC-compatibility-')
    assert page.locator('#roleName').inner_text()=='Minghoo · Admin'
    # External process changes refresh through ordinary polling.
    control_fixture['running']=False
    page.evaluate('refresh()');page.wait_for_function("!document.getElementById('serverStart').disabled")
    assert page.locator('#serverStop').is_disabled()
    control_fixture['running']=True
    page.evaluate('refresh()');page.wait_for_function("!document.getElementById('serverStop').disabled")
    # Updates use fixed control actions and remain disabled for running games/viewers.
    page.get_by_role('button',name='Server Management',exact=True).click()
    page.wait_for_timeout(250)
    page.evaluate("control={ok:true,running:true,updates:{serverConfigured:true,dashboardVersion:'test.1',dashboardRollbackAvailable:true},jobs:[]};renderControl()")
    assert page.locator('#updateServer').is_disabled()
    assert page.locator('#updateDashboard').is_enabled()
    page.evaluate("control.running=false;renderControl()")
    assert page.locator('#updateServer').is_enabled()
    page.locator('#updateServer').click()
    page.wait_for_timeout(200)
    assert any(path=='/api/thc/control' and payload=={'action':'update_server'} for path,payload in requests)
    page.evaluate("control.jobs=[{action:'update_server',state:'running'}];renderControl()")
    assert page.locator('#updateServer').is_disabled() and page.locator('#updateDashboard').is_disabled()
    assert page.locator('#serverStart').is_disabled() and page.locator('#serverStop').is_disabled()
    page.get_by_role('button',name='Server Management',exact=True).click();page.locator('#backupsTable').get_by_text('20261025000000-1234abcd',exact=True).wait_for()
    page.get_by_role('button',name='Analytics',exact=True).click();page.locator('#analyticsChart rect').first.wait_for(state='attached');assert page.locator('#analyticsChart rect').count()==72
    page.get_by_role('button',name='Overview',exact=True).click();page.locator('#quickSave').click();page.wait_for_function("document.getElementById('saveDetails').textContent.includes('pending')");assert page.locator('#quickSave').is_disabled()
    save_fixture.update({'busy':False,'request':{'state':'reported_success'}});page.evaluate('refresh()');page.wait_for_function("document.getElementById('saveDetails').textContent.includes('reported_success')");assert page.locator('#quickSave').is_enabled()
    # Image-only maps must not display falsely calibrated player markers.
    page.get_by_role('button',name='World Map',exact=True).click()
    image=page.evaluate("(()=>{const c=document.createElement('canvas');c.width=200;c.height=100;const x=c.getContext('2d');x.fillStyle='#173c38';x.fillRect(0,0,200,100);return c.toDataURL('image/png')})()")
    page.locator('#mapImage').set_input_files({'name':'fixture-map.png','mimeType':'image/png','buffer':base64.b64decode(image.split(',')[1])})
    page.locator('#mapState').filter(has_text='calibration needed').wait_for()
    assert page.locator('#worldPlotLarge circle[data-player]').count()==0
    def click_image(u,v):
        page.locator('#worldPlotLarge').scroll_into_view_if_needed()
        position=page.evaluate("([u,v])=>{const svg=document.getElementById('worldPlotLarge'),p=svg.createSVGPoint();p.x=30+u*416;p.y=28.5+v*208;const q=p.matrixTransform(svg.getScreenCTM());return {x:q.x,y:q.y}}",[u,v])
        page.mouse.click(position['x'],position['y'])
    for i,(x,y,u,v) in enumerate([(0,0,.2,.1),(1000,0,.2,.9),(0,1000,.8,.1)]):
        page.locator('#landmarkX').fill(str(x));page.locator('#landmarkY').fill(str(y));page.locator('#landmarkZ').fill('300');page.get_by_role('button',name='Mark landmark on map',exact=True).click();click_image(u,v)
        page.wait_for_function('(count)=>mapAnchors.length===count && mapPending===null',arg=i+1)
    page.locator('#mapState').filter(has_text='Local map draft').wait_for()
    circle=page.locator('#worldPlotLarge circle[data-player]');assert circle.count()==1
    assert abs(float(circle.get_attribute('cx'))-(30+.32*416))<1.5, page.evaluate('({anchors:mapAnchors,config:mapConfig})')
    assert abs(float(circle.get_attribute('cy'))-(28.5+.18*208))<1.5
    click_image(.5,.3);assert abs(float(page.locator('#teleportX').input_value())-250)<5 and abs(float(page.locator('#teleportY').input_value())-500)<5
    page.get_by_role('button',name='Use saved landmark 1',exact=True).click();assert page.locator('#teleportZ').input_value()=='300';assert page.locator('#teleportX').input_value()=='0'
    stored=page.evaluate("async()=>{const p=await mapRecord('get');return {schema:p.schema,anchors:p.config.anchors,image:p.image instanceof Blob,keys:Object.keys(p)}}")
    assert stored['schema']==1 and len(stored['anchors'])==3 and stored['image'] and 'token' not in stored['keys']
    page.get_by_role('button',name='Overview',exact=True).click()
    # Save a visual-review artifact using clearly synthetic fixture data.
    player['characterName']='Celestial';page.evaluate('refresh()');page.wait_for_timeout(300)
    page.locator('.hero').screenshot(path=str(root/'build/tests/header-preview-test-data.png'))
    page.screenshot(path=str(root/'build/tests/dashboard-preview.png'),full_page=True)
    page.set_viewport_size({'width':375,'height':812});overflow=page.evaluate("[...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>innerWidth+1 && getComputedStyle(e).display!=='none').slice(0,25).map(e=>({tag:e.tagName,id:e.id,cls:e.className,right:e.getBoundingClientRect().right,width:e.getBoundingClientRect().width}))");assert page.evaluate('document.documentElement.scrollWidth <= innerWidth'),overflow
    assert page.evaluate('localStorage.length === 0 && sessionStorage.length === 0')
    page.get_by_role('button',name='Lock dashboard',exact=True).click();assert page.locator('#login').is_visible() and page.locator('#app').is_hidden()
    assert page.locator('#createdToken').input_value()==''
    page.locator('#token').fill('viewer');page.get_by_role('button',name='Connect',exact=True).click();page.locator('#app').wait_for(state='visible');page.get_by_role('button',name='Administration',exact=True).click();assert page.get_by_role('button',name='Send broadcast').is_disabled()
    page.get_by_role('button',name='Server Management',exact=True).click();assert page.locator('#updateServer').is_disabled() and page.locator('#updateDashboard').is_disabled() and page.locator('#rollbackDashboard').is_disabled() and page.locator('#recoverIntegrations').is_disabled()
    page.reload();page.wait_for_function('mapReady() && !!mapImageUrl');assert page.locator('#token').input_value()=='';assert page.evaluate('mapAnchors.length')==3
    assert not errors,errors
    browser.close()
 print('PASS: browser auth/roles, safe names, telemetry, broadcast errors, items, teleport, console, access tokens, Discord settings, backups, analytics, persistent aspect-correct landmark maps, inverse clicks, thread-ID precision, save acceptance/completion distinction, responsive layout, logout and secret clearing')
finally:
 server.shutdown();server.server_close();thread.join()
