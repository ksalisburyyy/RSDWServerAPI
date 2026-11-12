import importlib.util
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import configure_thc_discord as setup
with tempfile.TemporaryDirectory(prefix='thc-discord-') as folder:
 p=Path(folder);(p/'api').mkdir()
 text='# keep comment\n[API]\nPort=8081\nBearerToken=fixture-owner\n[Discord]\nEnabled=false\nWebhookUrl=https://discord.com/api/webhooks/123/fixture-chat?thread_id=1111111111111111111\nUsername=THC Admin\n[Other]\nKeep=value\n'
 ini=p/'api/settings.ini';ini.write_text(text)
 config=p/'config.json';config.write_text(json.dumps({'server_root':str(p),'api_config':'api/settings.ini'}))
 private=p/'private.json';private.write_text(json.dumps({'AdminWebhookUrl':'https://discord.com/api/webhooks/456/fixture-admin'}))
 api,before,after,summary=setup.prepare(config,private)
 assert ini.read_text()==text and before==text and len(summary)==5
 assert '# keep comment' in after and 'Keep=value' in after and 'Username=\n' in after
 for label,key,threadkey,thread in setup.DESTINATIONS:
  assert threadkey+'='+thread in after and next(x for x in summary if x['lane']==label)['threadId']==thread
 assert 'thread_id=1111111111111111111' not in after and 'AdminWebhookUrl=https://discord.com/api/webhooks/456/fixture-admin' in after
 assert 'Enabled=true' in after
 called=[]
 def transport(url,payload):
  called.append((url,payload));return 204,0
 results=setup.test_routes(after,transport)
 assert len(results)==len(called)==5 and all(row['deliveryConfirmed'] for row in results)
 for (_,_,_,thread),(url,payload) in zip(setup.DESTINATIONS,called):
  assert 'thread_id='+thread in url and payload['allowed_mentions']=={'parse':[]}
 assert 'fixture-chat' not in json.dumps(results) and 'fixture-admin' not in json.dumps(results)

 assert setup.rewrite(after,{})==after
 # Idempotent rewrite and exact existing lane URLs are retained.
 ini.write_text(after);assert setup.prepare(config,private)[2]==after
 private.unlink();assert setup.prepare(config,private)[2]==after
 ini.write_text(text)
 try:setup.prepare(config,private);raise AssertionError('missing admin accepted')
 except ValueError:pass
 assert ini.read_text()==text
 print('PASS: five exact thread IDs as strings, private admin seed, existing chat/per-lane credentials preserved, no network calls, comments/unrelated settings preserved, dry preparation, idempotence and missing-secret refusal')
