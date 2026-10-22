#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=3,<4", "pyserial>=3.5,<4"]
# ///
"""Prism Desk JSON v1 client. Run with `uv run tools/prism_client.py --help`."""
import argparse
import asyncio
import getpass
import json
from pathlib import Path
import secrets
import sys
import time

SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'


def encode(command):
    message = dict(command)
    message.setdefault('v', 1)
    message.setdefault('id', secrets.randbelow(2**31))
    payload = json.dumps(message, separators=(',', ':'), ensure_ascii=True).encode()
    if len(payload) > 383:
        raise ValueError('Message exceeds 383 bytes; use shorter text.')
    return message, payload + b'\n'


async def request_ble(command, address=None):
    from bleak import BleakClient, BleakScanner
    message, payload = encode(command)
    if address:
        device = await BleakScanner.find_device_by_address(address, timeout=15)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda d, a: SERVICE in a.service_uuids or (a.local_name or '') == 'Prism Desk', timeout=20)
    if device is None:
        raise RuntimeError('Prism Desk not found. Hold ACTION for 2 seconds to open BLE.')
    async with BleakClient(device, pair=True, timeout=60) as client:
        for at in range(0, len(payload), 20):
            await client.write_gatt_char(RX, payload[at:at+20], response=True)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            response = json.loads(bytes(await client.read_gatt_char(TX)))
            if response.get('id') == message['id']:
                return response
            if response.get('ok') is False and 'id' not in response:
                return response
            await asyncio.sleep(.1)
    raise TimeoutError('Device did not acknowledge the command.')


def request_usb(command, port, reset=False):
    import serial
    message, payload = encode(command)
    with serial.Serial(port=None, baudrate=115200, timeout=.1) as connection:
        connection.dtr = False
        connection.rts = False
        connection.port = port
        connection.open()
        if reset:
            connection.dtr = False
            connection.rts = True
            time.sleep(.1)
            connection.rts = False
            time.sleep(.5)  # Release reset before asserting DTR/boot control.
            connection.dtr = True
        received = bytearray()

        def receive(expected_id, timeout):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                received.extend(connection.read(connection.in_waiting or 1))
                while b'\n' in received:
                    line, _, tail = received.partition(b'\n')
                    received[:] = tail
                    try:
                        response = json.loads(line)
                    except (ValueError, UnicodeError):
                        continue
                    if isinstance(response, dict) and response.get('id') == expected_id:
                        return response
            return None

        # Some USB drivers toggle reset lines on open. Establish readiness with
        # harmless reads so a Wi-Fi/timer command is sent exactly once afterward.
        ping, ping_data = encode({'type': 'get', 'what': 'state'})
        for _ in range(8):
            connection.write(ping_data)
            if receive(ping['id'], 1):
                break
        else:
            raise TimeoutError('No USB handshake. Retry with --reset-usb to restart ESP in normal mode.')
        connection.write(payload)
        connection.flush()
        response = receive(message['id'], 10)
        if response is not None:
            return response
    raise TimeoutError('No USB reply; check the port and firmware.')


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', help='BLE address; otherwise discover Prism Desk')
    parser.add_argument('--usb', help='Use the same JSON protocol via the ESP USB port')
    parser.add_argument('--reset-usb', action='store_true', help='Explicitly restart ESP after USB open; stops active timer')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('scan')
    sub.add_parser('state')
    stats = sub.add_parser('stats'); stats.add_argument('--output', type=Path)
    sub.add_parser('sync-time')
    widget = sub.add_parser('widget')
    widget.add_argument('--title', required=True); widget.add_argument('--value', required=True)
    widget.add_argument('--detail', default='')
    wifi = sub.add_parser('wifi'); wifi.add_argument('--ssid', required=True)
    wifi.add_argument('--open', action='store_true', help='Network without a password')
    timer = sub.add_parser('pomodoro')
    timer.add_argument('action', choices=['start', 'pause', 'resume', 'reset', 'configure'])
    timer.add_argument('--work', type=int, default=25); timer.add_argument('--break-min', type=int, default=5)
    raw = sub.add_parser('send'); raw.add_argument('json', help='Compact JSON object')
    args = parser.parse_args()
    if args.command == 'scan':
        from bleak import BleakScanner
        devices = await BleakScanner.discover(timeout=10, return_adv=True)
        for device, adv in devices.values():
            if SERVICE in adv.service_uuids or (adv.local_name or '') == 'Prism Desk':
                print(f'{device.address}  {adv.local_name or device.name}')
        return
    if args.command in ('state', 'stats'):
        command = {'type': 'get', 'what': args.command}
    elif args.command == 'sync-time':
        command = {'type': 'time', 'unix': int(time.time())}
    elif args.command == 'widget':
        command = {'type': 'widget', 'title': args.title, 'value': args.value, 'detail': args.detail}
    elif args.command == 'wifi':
        command = {'type': 'wifi', 'ssid': args.ssid,
                   'password': '' if args.open else getpass.getpass('Wi-Fi password: ')}
    elif args.command == 'pomodoro':
        command = {'type': 'pomodoro', 'action': args.action}
        if args.action == 'configure':
            command.update(work_min=args.work, break_min=args.break_min)
    else:
        command = json.loads(args.json)
        if not isinstance(command, dict):
            raise ValueError('Command must be a JSON object.')
    response = request_usb(command, args.usb, reset=args.reset_usb) if args.usb else await request_ble(command, args.address)
    formatted = json.dumps(response, ensure_ascii=False, indent=2) + '\n'
    if args.command == 'stats' and args.output:
        args.output.write_text(formatted, encoding='utf-8')
    print(formatted, end='')
    if not response.get('ok'):
        raise RuntimeError(response.get('error', 'Command rejected'))


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except (RuntimeError, TimeoutError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
