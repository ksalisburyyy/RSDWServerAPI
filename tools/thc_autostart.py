#!/usr/bin/env python3
"""Request one boot-time start through the existing private THC controller."""
import argparse
import json
import socket
import time
from pathlib import Path


def request(path, action):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(path)
        client.sendall((json.dumps({'action': action}) + '\n').encode())
        with client.makefile('rb') as stream:
            line = stream.readline(1048577)
        if len(line) > 1048576 or not line.endswith(b'\n'):
            raise ValueError('Invalid controller response')
        value = json.loads(line)
        if not isinstance(value, dict) or value.get('ok') is not True:
            raise ValueError('Controller refused request; inspect controller journal')
        return value


def autostart(path):
    deadline = time.monotonic() + 45
    while True:
        try:
            status = request(path, 'status')
            break
        except (OSError, ValueError):
            if time.monotonic() >= deadline:
                raise ValueError('Controller was not ready within 45 seconds')
            time.sleep(1)
    if status.get('running') is True:
        print('Game is already running; no start requested.')
        return
    if status.get('canStart') is not True:
        raise ValueError('Controller does not permit starting the game')
    job = request(path, 'start').get('job')
    if not isinstance(job, str) or not job:
        raise ValueError('Controller did not return a start job')
    deadline = time.monotonic() + 75
    while time.monotonic() < deadline:
        status = request(path, 'status')
        match = next((row for row in status.get('jobs', []) if row.get('id') == job), None)
        if match and match.get('state') == 'failed':
            raise ValueError('Boot-time start failed; inspect dashboard Operation Jobs')
        if match and match.get('state') == 'complete':
            if status.get('running') is not True:
                raise ValueError('Start job completed but the game process exited')
            print('Game process started; check dashboard for world/API readiness.')
            return
        time.sleep(1)
    raise ValueError('Boot-time start job did not complete within 75 seconds')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True)
    args = parser.parse_args()
    try:
        config = json.loads(Path(args.config).read_text())
        autostart(config['socket'])
    except (OSError, ValueError, KeyError, TypeError):
        raise SystemExit('THC boot-time start failed; inspect the controller and dashboard. No retry loop was installed.')


if __name__ == '__main__':
    main()
