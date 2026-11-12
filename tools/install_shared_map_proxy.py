#!/usr/bin/env python3
"""Add a map-only upload allowance to the reviewed THC Nginx site."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

path = Path('/etc/nginx/sites-available/thc-dashboard')
marker = '    location / {\n        limit_req zone=thc_dashboard burst=60 nodelay;'
block = '''    # THC shared map: larger uploads only on this authenticated endpoint.
    location = /api/thc/map {
        client_max_body_size 14m;
        limit_req zone=thc_dashboard burst=60 nodelay;
        proxy_pass http://127.0.0.1:5001;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header Authorization $http_authorization;
        proxy_set_header X-Forwarded-Proto https;
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }

'''

def main():
    if os.geteuid() != 0:
        raise SystemExit('Run this helper as root. It only updates the THC Nginx site.')
    before = path.read_text()
    if '    location = /api/thc/map {' in before:
        raise SystemExit('Shared-map location already exists; no changes made.')
    if before.count(marker) != 1:
        raise SystemExit('THC site differs from the reviewed layout; no changes made.')
    after = before.replace(marker, block + marker)
    backup = path.with_name(path.name + '.before-map-' + str(time.time_ns()))
    shutil.copy2(path, backup)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix='.thc-map-')
    try:
        with os.fdopen(fd, 'w') as stream:
            stream.write(after);stream.flush();os.fsync(stream.fileno())
        os.chmod(temporary, path.stat().st_mode & 0o777)
        os.replace(temporary, path)
        if subprocess.run(['/usr/sbin/nginx', '-t']).returncode:
            shutil.copy2(backup, path)
            raise SystemExit('Nginx validation failed; previous site restored, no reload performed.')
        if subprocess.run(['/bin/systemctl', 'reload', 'nginx']).returncode:
            shutil.copy2(backup, path)
            subprocess.run(['/usr/sbin/nginx', '-t'], check=True)
            subprocess.run(['/bin/systemctl', 'reload', 'nginx'], check=True)
            raise SystemExit('Reload failed; previous site restored.')
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    print('Map upload allowance installed; other API limits unchanged. Backup: ' + str(backup))

if __name__ == '__main__':
    main()
