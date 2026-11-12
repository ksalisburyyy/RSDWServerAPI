#!/usr/bin/env python3
"""Real controller HTTP + two independent browser profiles; no game or VPS."""
import base64
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import threading
import urllib.error
from http.server import ThreadingHTTPServer
from playwright.sync_api import sync_playwright

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('thc_control',root/'tools/thc_control.py')
control=importlib.util.module_from_spec(spec);spec.loader.exec_module(control)
with tempfile.TemporaryDirectory(prefix='thc-shared-map-') as directory:
 p=Path(directory);server_root=p/'game';server_root.mkdir();(server_root/'Saved').mkdir()
 shutil.copy2('/bin/sleep',server_root/'game-fixture')
 (server_root/'api.ini').write_text('[API]\nBearerToken=fixture-secret\nPort=1\n')
 html=(root/'src/api/dashboard.h').read_text().split('R"THCHTML(',1)[1].rsplit(')THCHTML"',1)[0]
 (p/'dashboard.html').write_text(html)
 config={'server_root':str(server_root),'save_directory':'Saved','backup_directory':str(p/'backups'),'game_executable':'game-fixture','mode':'disabled','api_config':'api.ini','dashboard_file':str(p/'dashboard.html')}
 controller=control.Controller(config);proxy=control.DashboardProxy(controller,config)
 class Offline:
  def open(self,*args,**kwargs):raise urllib.error.URLError('local game is intentionally absent')
 proxy.opener=Offline();web=ThreadingHTTPServer(('127.0.0.1',0),control.DashboardHandler);web.proxy=proxy
 thread=threading.Thread(target=web.serve_forever,daemon=True);thread.start()
 try:
  with sync_playwright() as pw:
   browser=pw.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox'])
   contexts=[browser.new_context() for _ in range(2)];pages=[c.new_page() for c in contexts];errors=[]
   for page in pages:
    page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
   def login(page):
    page.goto(f'http://127.0.0.1:{web.server_port}/dashboard')
    page.locator('#token').fill('fixture-secret');page.get_by_role('button',name='Connect',exact=True).click()
    page.wait_for_function("role==='admin' && sharedMapSupported && !sharedMapBusy")
    page.get_by_role('button',name='World Map',exact=True).click()
   a,b=pages;login(a)
   # Use a noisy PNG larger than the legacy 64 KB request limit.
   image=a.evaluate("(()=>{const c=document.createElement('canvas');c.width=200;c.height=200;const x=c.getContext('2d'),d=x.createImageData(200,200);for(let i=0;i<d.data.length;i++)d.data[i]=(i*17+(i>>5)*37)%256;x.putImageData(d,0,0);return c.toDataURL('image/png').split(',')[1]})()")
   # Make deterministic incompressible image data if needed.
   if len(image)<90000:
    image=a.evaluate("(()=>{const c=document.createElement('canvas');c.width=200;c.height=200;const x=c.getContext('2d'),d=x.createImageData(200,200);for(let i=0;i<d.data.length;i+=65536)crypto.getRandomValues(d.data.subarray(i,Math.min(i+65536,d.data.length)));x.putImageData(d,0,0);return c.toDataURL('image/png').split(',')[1]})()")
   anchors=[{'x':0,'y':0,'u':.1,'v':.1,'z':123},{'x':1000,'y':0,'u':.9,'v':.1,'z':234},{'x':0,'y':1000,'u':.1,'v':.9,'z':345}]
   profile={'schema':1,'config':{'anchors':anchors},'anchors':anchors,'image':{'mime':'image/png','data':image}}
   assert len(json.dumps(profile))>65536
   def import_profile(page,profile):
    page.locator('#importMap').set_input_files({'name':'THC-map-profile.json','mimeType':'application/json','buffer':json.dumps(profile).encode()})
    page.wait_for_function('mapReady() && mapDirty && mapAnchors.length===3')
   import_profile(a,profile);a.locator('#publishMap').click();a.wait_for_function('sharedMapActive && !mapDirty && !sharedMapBusy')
   revision=proxy.shared_map.read()['revision'];assert revision
   assert proxy.shared_map.read()['profile']['image']==profile['image']
   for mime in ['image/jpeg','image/webp']:
    encoded=a.evaluate("async(mime)=>{const image=await createImageBitmap(mapBlob),c=document.createElement('canvas');c.width=image.width;c.height=image.height;c.getContext('2d').drawImage(image,0,0);image.close();return c.toDataURL(mime).split(',')[1]}",mime)
    candidate={**profile,'image':{'mime':mime,'data':encoded}}
    assert control.SharedMap.validate(candidate)['image']==candidate['image']

   login(b);b.wait_for_function('sharedMapActive && mapReady() && !!mapImageUrl')
   assert b.evaluate('mapAnchors')==anchors
   assert b.evaluate("async()=>!(await mapRecord('get'))") # No local profile required.
   # Legacy migration reads the existing IndexedDB record without recalibration.
   b.evaluate("async()=>{await mapRecord('put',{schema:1,config:mapConfig,anchors:mapAnchors,image:mapBlob})}")
   with b.expect_download() as legacy_download:b.evaluate((root/'tools/export_browser_map.js').read_text())
   legacy=json.loads(Path(legacy_download.value.path()).read_text());assert legacy['anchors']==anchors
   assert legacy['config']==profile['config'] and 'fixture-secret' not in json.dumps(legacy)
   b.evaluate("async()=>{await mapRecord('delete')}")

   assert b.evaluate('({x:MapModel.project(mapConfig,500,500).u,y:MapModel.project(mapConfig,500,500).v})')=={'x':.5,'y':.5}
   # Export contains the original calibrated coordinates, not credentials.
   with b.expect_download() as download:b.locator('#exportMap').click()
   exported=json.loads(Path(download.value.path()).read_text());assert exported['anchors']==anchors and 'fixture-secret' not in json.dumps(exported)
   # Another admin's local draft must not silently overwrite a newer shared revision.
   import_profile(b,profile)
   import_profile(a,profile);a.locator('#publishMap').click();a.wait_for_function('sharedMapActive && !mapDirty && !sharedMapBusy')
   newer=proxy.shared_map.read()['revision'];assert newer!=revision
   b.locator('#publishMap').click();b.locator('#result').filter(has_text='Shared map changed').wait_for();assert proxy.shared_map.read()['revision']==newer
   b.locator('#loadSharedMap').click();b.wait_for_function('sharedMapActive && !mapDirty && !sharedMapBusy')
   # A clean viewing browser picks up a newer shared map on its next poll.
   fresh=proxy.shared_map.read();proxy.shared_map.publish({'baseRevision':fresh['revision'],'profile':fresh['profile']})
   next_revision=proxy.shared_map.read()['revision']
   b.evaluate('sharedMapChecked=0;refresh()')
   b.wait_for_function('(expected)=>sharedMapRevision===expected',arg=next_revision)
   # Restart the controller-side store; the browser still restores from disk.
   proxy.shared_map=control.SharedMap(server_root/'thc/shared-map.json')
   b.reload();login(b);b.wait_for_function('sharedMapActive && mapReady()');assert b.evaluate('mapAnchors')==anchors
   assert not errors,errors
   for context in contexts:context.close()
   browser.close()
  print('PASS: real authenticated controller HTTP, image upload above 64 KB, publish/import/export, unchanged landmarks, second clean browser loads automatically, no local storage required, stale-publish refusal, reload and server-store restart persistence')
 finally:web.shutdown();web.server_close();thread.join()
