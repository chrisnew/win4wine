#!/usr/bin/env python3
"""Show what is on the local network in XP's My Network Places.

Follows mDNS/DNS-SD through Avahi (D-Bus) and UPnP through SSDP, and keeps one
shortcut per device and service in the user's Network Shortcuts folder: web
pages, printers, SSH and computers that announce nothing else. A web page opens
in the Linux browser (XP Explorer's iexplore.exe stand-in), SSH opens a
terminal, and what cannot be opened shows XP's "not accessible" message.

Names and addresses come from the network and are not trusted: only private and
link-local addresses count, file names are cleaned, URLs are rebuilt from the
announced address (http and https only, percent encoded), SSH gets that address and never the announced name, and
a UPnP device description is read with a time and size limit and without a DTD.

Only shortcuts that this program made are changed or removed; their list is kept
in C:\\xp\\network-places\\state.json. It removes them all when it starts and
when it is stopped, so none stay behind from a crash or another network.
--remove takes them out and ends.
"""
import argparse
import fcntl
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import pwd
import queue
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import unicodedata
import urllib.parse
import urllib.request
from xml.parsers import expat

ROOT = Path(__file__).resolve().parents[1]
STATE = 'drive_c/xp/network-places'
WINDOWS_STATE = r'C:\xp\network-places'
BROWSER = r'C:\xp\bin\iexplore.exe'
SHELL32 = r'C:\windows\system32\shell32.dll'
# XP's icons in shell32.dll.
ICONS = {'web': 13, 'computer': 15, 'printer': 16, 'router': 18, 'ssh': 24, 'media': 40}
KINDS = ('web', 'printer', 'ssh', 'computer')
SERVICE_TYPES = {
    '_http._tcp': 'web', '_https._tcp': 'web',
    '_ipp._tcp': 'printer', '_ipps._tcp': 'printer', '_printer._tcp': 'printer', '_pdl-datastream._tcp': 'printer',
    '_ssh._tcp': 'ssh', '_sftp-ssh._tcp': 'ssh',
    '_workstation._tcp': 'computer', '_smb._tcp': 'computer',
}
SETTLE, LONGEST_WAIT = 3.0, 10.0   # Changes are collected: each write starts Wine.
SEARCH_INTERVAL = 600              # Between M-SEARCHes; devices announce themselves in between.
MAX_SERVICES, MAX_UPNP = 256, 64
MAX_DESCRIPTION = 256 * 1024
SSDP = ('239.255.255.250', 1900)


# What was announced, and what to show of it. No input or output here.

def clean(text, limit=60):
    """An announced name as part of a file name, or '' when nothing is left."""
    text = unicodedata.normalize('NFC', str(text))
    # Control, format (bidirectional overrides) and unassigned characters, and
    # the line and paragraph separators.
    text = ''.join(' ' if c in '\\/:*?"<>|' or c.isspace() else c for c in text
                   if c.isspace() or unicodedata.category(c)[0] != 'C')
    text = ' '.join(text.split())[:limit].strip(' .')
    if re.fullmatch(r'(?i)(con|prn|aux|nul|com\d|lpt\d)(\..*)?', text):
        text = '_' + text
    return text


def address(text):
    """TEXT as an IPv4 address of a device on a local network (private or link-local), or None."""
    try:
        ip = ipaddress.IPv4Address(text)
    except ValueError:
        return None
    if not ip.is_private or ip.is_loopback or ip.is_unspecified or int(ip) == 0xffffffff:
        return None
    return str(ip)


def make_url(scheme, ip, port, path):
    """An http or https URL of IP: no quote, backslash or space gets through."""
    if scheme not in ('http', 'https') or not address(ip) or not 0 < port < 65536:
        return ''
    netloc = ip if port == (80 if scheme == 'http' else 443) else f'{ip}:{port}'
    path = urllib.parse.quote(path[:1024], safe="/?#[]@!$&'()*+,;=:%-._~", errors='replace')
    return f'{scheme}://{netloc}/{path.lstrip("/")}'


def rebase_url(value, ip, base=None):
    """A URL that a device gave (relative to BASE), on the address it came from."""
    try:
        parts = urllib.parse.urlsplit(urllib.parse.urljoin(base, value) if base else value)
        port = parts.port or (443 if parts.scheme == 'https' else 80)
    except ValueError:
        return ''
    path = parts.path + (f'?{parts.query}' if parts.query else '') + (f'#{parts.fragment}' if parts.fragment else '')
    return make_url(parts.scheme, ip, port, path)


def txt_record(items):
    """DNS-SD TXT strings (bytes) as {lower-case key: value}; the first of a key counts."""
    record = {}
    for item in items:
        key, found, value = bytes(item).partition(b'=')
        if found:
            record.setdefault(key.decode('ascii', 'replace').lower(), value.decode('utf-8', 'replace'))
    return record


def mdns_service(kind_of, name, host, ip, port, txt):
    """A resolved DNS-SD service as a service dict, or None."""
    kind, ip = SERVICE_TYPES.get(kind_of), address(ip)
    if not kind or not ip:
        return None
    record = txt_record(txt)
    url = ''
    if kind == 'web':
        url = make_url('https' if kind_of == '_https._tcp' else 'http', ip, port, record.get('path', '/'))
        if not url:
            return None
    elif kind == 'printer' and record.get('adminurl'):
        url = rebase_url(record['adminurl'], ip)
    elif kind == 'ssh' and not 0 < port < 65536:
        return None
    host = host.rstrip('.')
    if host.lower().endswith('.local'):
        host = host[:-6]
    return dict(source='mdns', kind=kind, ip=ip, port=port, url=url, host=host, name=name, icon='')


def parse_ssdp(data):
    """('notify' or 'response', {lower-case header: value}), or None for anything else."""
    lines = data.decode('latin-1').split('\r\n')
    start = lines[0].upper()
    if start.startswith('NOTIFY '):
        kind = 'notify'
    elif start.startswith('HTTP/1.') and start.split()[1:2] == ['200']:
        kind = 'response'
    else:
        return None
    headers = {}
    for line in lines[1:]:
        name, found, value = line.partition(':')
        if found:
            headers.setdefault(name.strip().lower(), value.strip())
    return kind, headers


def ssdp_event(data, source):
    """What an SSDP message from SOURCE says: ('gone', UDN), ('alive', UDN,
    LOCATION, SECONDS) for a root device, or None."""
    message = parse_ssdp(data)
    if not message or not address(source):
        return None
    headers = message[1]
    udn = headers.get('usn', '').split('::')[0]
    if not udn or len(udn) > 200:
        return None
    if headers.get('nts', '').lower() == 'ssdp:byebye':
        return 'gone', udn
    target = headers.get('nt' if message[0] == 'notify' else 'st', '')
    if target.lower() != 'upnp:rootdevice':
        return None
    location = headers.get('location', '')
    try:
        parts = urllib.parse.urlsplit(location)
        parts.port
    except ValueError:
        return None
    # The description must come from the device that announced it.
    if parts.scheme != 'http' or parts.hostname != source or len(location) > 1024 \
            or any(c <= ' ' or c > '~' for c in location):
        return None
    age = re.search(r'max-age\s*=\s*(\d{1,9})', headers.get('cache-control', ''), re.IGNORECASE)
    return 'alive', udn, location, min(max(int(age.group(1)), 60), 86400) if age else 1800


def parse_description(data):
    """The root device's friendlyName, deviceType and presentationURL of a UPnP
    device description, or None. A document with a DTD is refused: no entities."""
    wanted = ('friendlyName', 'deviceType', 'presentationURL')
    stack, found = [], {}

    def start(name, _):
        stack.append(name.rsplit(':', 1)[-1])
        if len(stack) > 32:
            raise ValueError('too deep')

    def text(value):
        if len(stack) == 3 and stack[:2] == ['root', 'device'] and stack[2] in wanted:
            found[stack[2]] = (found.get(stack[2], '') + value)[:1024]

    def doctype(*_):
        raise ValueError('DTD')

    parser = expat.ParserCreate()
    parser.StartElementHandler = start
    parser.EndElementHandler = lambda _: stack.pop()
    parser.CharacterDataHandler = text
    parser.StartDoctypeDeclHandler = doctype
    try:
        parser.Parse(data, True)
    except (expat.ExpatError, ValueError, LookupError):
        return None
    result = {key: value.strip() for key, value in found.items()}
    return result if 'friendlyName' in result or 'deviceType' in result else None


def upnp_service(description, location, ip):
    """A UPnP device as a service dict."""
    device = description.get('deviceType', '').split(':device:')[-1].split(':')[0]
    value = description.get('presentationURL', '')
    url = rebase_url(value, ip, location) if value else ''
    if device in ('Printer', 'PrinterBasic', 'PrinterEnhanced'):
        kind = 'printer'
    else:
        kind = 'web' if url else 'computer'
    icon = 'router' if device in ('InternetGatewayDevice', 'WLANAccessPointDevice') \
        else 'media' if device.startswith('Media') else ''
    return dict(source='upnp', kind=kind, ip=ip, port=0, url=url, host='',
                name=description.get('friendlyName', ''), icon=icon)


class Registry:
    """The services seen, by a key of their source; each stays until removed or expired."""

    def __init__(self, grace):
        self.grace = grace
        self.services = {}   # key: [service, deadline or None]

    def add(self, key, service, now, lifetime=None):
        if key not in self.services and len(self.services) >= MAX_SERVICES:
            return False
        old = self.services.get(key)
        self.services[key] = [service, None if lifetime is None else now + lifetime + self.grace]
        return not old or old[0] != service

    def refresh(self, key, now, lifetime):
        if key in self.services:
            self.services[key][1] = now + lifetime + self.grace

    def remove(self, key, now):
        """Gone, but kept for the grace period in case it comes back at once."""
        item = self.services.get(key)
        if item and (item[1] is None or item[1] > now + self.grace):
            item[1] = now + self.grace

    def expire(self, now):
        """Drop what has been gone long enough; returns its keys."""
        gone = [key for key, (_, until) in self.services.items() if until is not None and until <= now]
        for key in gone:
            del self.services[key]
        return gone

    def deadline(self):
        return min((until for _, until in self.services.values() if until is not None), default=None)

    def values(self):
        return [self.services[key][0] for key in sorted(self.services, key=repr)]


def devices(services):
    """The services grouped into devices: those that share an address or a host name."""
    groups = []   # (tokens, services)
    for service in services:
        tokens = {('ip', service['ip'])}
        if service['host'] and service['host'].lower() != 'localhost':
            tokens.add(('host', service['host'].lower()))
        members = [service]
        for group in [g for g in groups if g[0] & tokens]:
            tokens |= group[0]
            members = group[1] + members
            groups.remove(group)
        groups.append((tokens, members))
    return [members for _, members in groups]


def device_name(members):
    """The UPnP name, else the host name, else the service's name, else the address."""
    for names in ((s['name'] for s in members if s['source'] == 'upnp'), (s['host'] for s in members),
                  (s['name'] for s in members), (s['ip'] for s in members)):
        usable = sorted(filter(None, map(clean, names)), key=str.casefold)
        if usable:
            return usable[0]
    return 'Unknown device'


def unreachable(name):
    """The arguments of linux-launch.exe's "not accessible" message about NAME."""
    return ['--unreachable', re.sub(r'[^A-Za-z0-9._-]+', '-', name).strip('-.').upper() or 'COMPUTER']


def device_entries(members, terminal):
    """The shortcuts of one device: {name, kind, target, arguments, icon, description}."""
    name = device_name(members)
    launcher = WINDOWS_STATE + r'\linux-launch.exe'
    result = []

    def entry(title, kind, icon, url='', command=None, description=''):
        result.append(dict(name=title, kind=kind, icon=ICONS[icon],
                           target=BROWSER if url else launcher,
                           arguments=url or subprocess.list2cmdline(command or unreachable(name)),
                           description=description or url))

    def of(kind):
        return sorted((s for s in members if s['kind'] == kind), key=lambda s: (s['url'], s['ip'], s['port']))

    # One page for each scheme, port and path, whichever address announced it.
    pages = {}
    for service in sorted(members, key=lambda s: (s['url'], s['ip'])):
        if service['url'] and service['kind'] == 'web':
            parts = urllib.parse.urlsplit(service['url'])
            port = parts.port or (443 if parts.scheme == 'https' else 80)
            pages.setdefault((parts.scheme, port, parts.path, parts.query), service['url'])
    # The same page over https and http: the http one, without a certificate warning.
    for scheme, port, path, query in list(pages):
        if scheme == 'https' and port == 443 and ('http', 80, path, query) in pages:
            del pages[(scheme, port, path, query)]

    printers = of('printer')
    if printers:
        url = next((s['url'] for s in printers if s['url']), '')
        if not url and pages:
            url = pages[min(pages, key=lambda k: (k[1] not in (80, 443), k))]
        for key in [k for k, v in pages.items() if v == url]:
            del pages[key]
        entry(f'Printer on {name}', 'printer', 'printer', url=url, description=url or printers[0]['ip'])
    # The standard ports first, so that they get the name without a port.
    for index, key in enumerate(sorted(pages, key=lambda k: (k[1] not in (80, 443), k))):
        title = f'Web page on {name}' + (f' ({key[1]})' if index else '')
        entry(title, 'web', 'web', url=pages[key])
    if terminal:
        ports = {}
        for service in of('ssh'):
            ports.setdefault(service['port'], service['ip'])
        for index, (port, ip) in enumerate(sorted(ports.items(), key=lambda p: (p[0] != 22, p[0]))):
            # The address, never the announced name, and after "--": a name such
            # as -oProxyCommand=... would be an option that runs a command.
            script = 'ssh "$@" || { echo; echo "Press Enter to close this window."; read -r _; }'
            command = [terminal, '-T', f'SSH on {name}', '-e', 'sh', '-c', script, 'ssh', '-p', str(port), '--', ip]
            entry(f'SSH on {name}' + (f' ({port})' if index else ''), 'ssh', 'ssh', command=command,
                  description=ip if port == 22 else f'{ip} port {port}')
    if not result:
        icon = next((s['icon'] for s in members if s['icon']), 'computer')
        entry(name, 'computer', icon, description=sorted(s['ip'] for s in members)[0])
    return result


def shortcuts(services, terminal, show=KINDS, limit=30):
    """The shortcuts for the services, at most LIMIT: web pages first, bare computers last."""
    found = [e for members in devices(services) for e in device_entries(members, terminal) if e['kind'] in show]
    found.sort(key=lambda e: (KINDS.index(e['kind']), e['name'].casefold(), e['description']))
    return found[:limit]


# The shortcuts in the prefix.

class Folder:
    def __init__(self, prefix, wine, user=None):
        user = user or pwd.getpwuid(os.getuid()).pw_name
        self.prefix, self.wine = prefix, wine
        relative = f'users/{user}/AppData/Roaming/Microsoft/Windows/Network Shortcuts'
        self.links = prefix / 'drive_c' / relative
        self.windows_links = 'C:\\' + relative.replace('/', '\\')
        self.state_dir = prefix / STATE
        self.state_file = self.state_dir / 'state.json'

    def load(self):
        """{file name: hash} of the shortcuts made here."""
        try:
            links = json.loads(self.state_file.read_text())['links']
            return {n: h for n, h in links.items() if n == Path(n).name and n.endswith('.lnk')}
        except (OSError, ValueError, KeyError, AttributeError):
            return {}

    def save(self, links):
        self.state_dir.mkdir(parents=True, exist_ok=True)
        temporary = self.state_file.with_suffix('.tmp')
        temporary.write_text(json.dumps({'links': links}, ensure_ascii=False, indent=1) + '\n')
        temporary.replace(self.state_file)

    def remove_all(self):
        old = self.load()
        for name in old:
            (self.links / name).unlink(missing_ok=True)
        if old:
            self.save({})
        return len(old)

    def install_launcher(self):
        source = ROOT / '.local/bridge/linux-launch.exe'
        target = self.state_dir / 'linux-launch.exe'
        self.state_dir.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != source.read_bytes():
            temporary = target.with_suffix('.tmp')
            shutil.copyfile(source, temporary)
            temporary.replace(target)

    def named(self, entries, old):
        """{file name: entry}, without two of a name or one of the user's own."""
        ours = {n.casefold() for n in old}
        try:
            used = {p.name.casefold() for p in self.links.iterdir()} - ours
        except FileNotFoundError:
            used = set()
        result = {}
        for item in entries:
            name, number = f'{item["name"]}.lnk', 2
            while name.casefold() in used:
                name, number = f'{item["name"]} ({number}).lnk', number + 1
            used.add(name.casefold())
            result[name] = item
        return result

    def line(self, name, item):
        description = ''.join(c for c in item['description'] if unicodedata.category(c)[0] not in 'CZ' or c == ' ')
        return '\t'.join((f'{self.windows_links}\\{name}', item['target'], item['arguments'], '',
                          f'{SHELL32}|{item["icon"]}', description[:250]))

    def sync(self, entries):
        """Bring the shortcuts up to date; returns (written, removed)."""
        old = self.load()
        new = self.named(entries, old)
        lines = {name: self.line(name, item) for name, item in new.items()}
        hashes = {name: hashlib.sha256(line.encode()).hexdigest() for name, line in lines.items()}
        gone = old.keys() - new.keys()
        for name in gone:
            (self.links / name).unlink(missing_ok=True)
        write = [n for n in new if old.get(n) != hashes[n] or not (self.links / n).is_file()]
        # Listed before they are written, so that an interrupted write leaves none unlisted.
        self.save({name: hashes[name] if name not in write else '' for name in new})
        if not write:
            return 0, len(gone)
        self.install_launcher()
        env = dict(os.environ, WINEPREFIX=str(self.prefix))
        # Saving a link would otherwise ask winemenubuilder to make a Linux menu entry.
        overrides = env.get('WINEDLLOVERRIDES', '')
        env['WINEDLLOVERRIDES'] = (overrides + ';' if overrides else '') + 'winemenubuilder.exe=d'
        env.pop('WINEARCH', None)
        result = subprocess.run([self.wine, str(ROOT / '.local/bridge/linux-links.exe')], env=env,
                                input=''.join(lines[n] + '\n' for n in write), capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=120)
        done = {line[3:] for line in result.stdout.split('\n') if line.startswith('ok ')}
        done = {n for n in write if f'{self.windows_links}\\{n}' in done}
        if len(done) != len(write):
            print('network-places: linux-links.exe wrote', len(done), 'of', len(write), result.returncode,
                  result.stdout[-1000:], result.stderr[-1000:], file=sys.stderr, flush=True)
        self.save({name: hashes[name] if name not in write or name in done else '' for name in new})
        return len(done), len(gone)


# The network.

def interfaces():
    """{(index, IPv4 address)} of the interfaces that are up, without loopback."""
    found = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        for index, name in socket.if_nameindex():
            try:
                reply = fcntl.ioctl(probe, 0x8915, struct.pack('256s', name.encode()[:15]))  # SIOCGIFADDR
            except OSError:
                continue
            ip = socket.inet_ntoa(reply[20:24])
            if address(ip):
                found.add((index, ip))
    return found


def membership(index):
    """struct ip_mreqn: the SSDP group on the interface of INDEX."""
    return struct.pack('4s4si', socket.inet_aton(SSDP[0]), bytes(4), index)


def fetch(location):
    """The device description at LOCATION: no proxy, no redirect, bounded in size and time."""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *_):
            return None

    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect)
    end = time.monotonic() + 10
    with opener.open(location, timeout=5) as response:
        data = bytearray()
        while True:
            chunk = response.read1(65536)
            if not chunk:
                return bytes(data)
            data += chunk
            if len(data) > MAX_DESCRIPTION or time.monotonic() > end:
                raise OSError('device description too large or too slow')


class Discovery:
    """mDNS and SSDP into a Registry, and the Registry into the Folder, on a GLib main loop."""

    def __init__(self, folder, terminal, show, limit, grace):
        from gi.repository import Gio, GLib
        self.Gio, self.GLib = Gio, GLib
        self.folder, self.terminal, self.show, self.limit = folder, terminal, show, limit
        self.registry = Registry(grace)
        self.loop = GLib.MainLoop()
        self.write_timer = self.expiry_timer = self.address_timer = None
        self.changed_since = None
        # UPnP: {UDN: (location, lifetime)} announced; a failed one is asked again later.
        self.upnp, self.failed = {}, {}
        self.fetches = queue.Queue(MAX_UPNP)
        threading.Thread(target=self.fetcher, daemon=True).start()
        self.addresses = set()
        self.listener = None
        self.searcher = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.searcher.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
        self.searcher.setblocking(False)
        GLib.io_add_watch(self.searcher.fileno(), GLib.PRIORITY_DEFAULT, GLib.IOCondition.IN,
                          lambda *_: self.receive(self.searcher))
        self.netlink = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
        self.netlink.bind((0, 0x10))  # RTMGRP_IPV4_IFADDR
        self.netlink.setblocking(False)
        GLib.io_add_watch(self.netlink.fileno(), GLib.PRIORITY_DEFAULT, GLib.IOCondition.IN, self.address_event)
        self.join()
        self.search()
        GLib.timeout_add_seconds(SEARCH_INTERVAL, lambda: self.search() or True)
        self.start_avahi()

    # Changes

    def changed(self):
        """Write the shortcuts once the changes have settled."""
        now = time.monotonic()
        if self.changed_since is None:
            self.changed_since = now
        if self.write_timer:
            self.GLib.source_remove(self.write_timer)
        delay = max(0.0, min(SETTLE, self.changed_since + LONGEST_WAIT - now))
        self.write_timer = self.GLib.timeout_add(int(delay * 1000), self.write)
        self.schedule_expiry()

    def write(self):
        self.write_timer = self.changed_since = None
        try:
            written, removed = self.folder.sync(shortcuts(self.registry.values(), self.terminal,
                                                          self.show, self.limit))
            if written or removed:
                print(f'network-places: {written} written, {removed} removed', flush=True)
        except (subprocess.SubprocessError, OSError) as failure:
            print('network-places:', failure, file=sys.stderr, flush=True)
        return False

    def schedule_expiry(self):
        if self.expiry_timer:
            self.GLib.source_remove(self.expiry_timer)
            self.expiry_timer = None
        deadline = self.registry.deadline()
        if deadline is not None:
            delay = max(0.0, deadline - time.monotonic())
            self.expiry_timer = self.GLib.timeout_add(int(delay * 1000) + 100, self.expire)

    def expire(self):
        self.expiry_timer = None
        gone = self.registry.expire(time.monotonic())
        if gone:
            for key in gone:
                if key[0] == 'upnp':
                    self.upnp.pop(key[1], None)  # Fetched again when it returns.
            self.changed()
        else:
            self.schedule_expiry()
        return False

    # UPnP

    def join(self):
        """Listen for announcements on every interface there is now."""
        self.addresses = interfaces()
        if self.listener:
            self.GLib.source_remove(self.listen_watch)
            self.listener.close()
            self.listener = None
        try:
            listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(SSDP)
            listener.setblocking(False)
        except OSError as failure:
            print('network-places: no SSDP announcements:', failure, file=sys.stderr, flush=True)
            return
        for index in {index for index, _ in self.addresses}:
            try:
                listener.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership(index))
            except OSError as failure:
                print('network-places: no multicast on interface', index, failure, file=sys.stderr, flush=True)
        self.listener = listener
        self.listen_watch = self.GLib.io_add_watch(listener.fileno(), self.GLib.PRIORITY_DEFAULT,
                                                   self.GLib.IOCondition.IN, lambda *_: self.receive(listener))

    def search(self, again=True):
        message = ('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\n'
                   'MX: 3\r\nST: upnp:rootdevice\r\n\r\n').encode()
        for index, _ in self.addresses:
            try:
                self.searcher.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, membership(index))
                self.searcher.sendto(message, SSDP)
            except OSError:
                pass  # The interface went away.
        if again:  # UDP: once more, in case it was lost.
            self.GLib.timeout_add_seconds(2, lambda: self.search(False) or False)

    def receive(self, source):
        for _ in range(100):
            try:
                data, (ip, _) = source.recvfrom(4096)
            except BlockingIOError:
                break
            except OSError:
                break
            if ip not in {own for _, own in self.addresses}:
                self.ssdp(data, ip)
        return True

    def ssdp(self, data, ip):
        event = ssdp_event(data, ip)
        if not event:
            return
        now = time.monotonic()
        if event[0] == 'gone':
            if self.upnp.pop(event[1], None):
                self.registry.remove(('upnp', event[1]), now)
                self.schedule_expiry()
            return
        _, udn, location, lifetime = event
        old = self.upnp.get(udn)
        if not old and len(self.upnp) >= MAX_UPNP:
            return
        self.upnp[udn] = (location, lifetime)
        if old and old[0] == location and now >= self.failed.get(location, 0):
            if ('upnp', udn) in self.registry.services:
                self.registry.refresh(('upnp', udn), now, lifetime)
                self.schedule_expiry()
                return
            if location not in self.failed:
                return  # Being fetched.
        if now < self.failed.get(location, 0):
            return
        self.failed.pop(location, None)
        try:
            self.fetches.put_nowait((udn, location, ip))
        except queue.Full:
            del self.upnp[udn]  # At its next announcement.

    def fetcher(self):
        """In a thread of its own: a slow device holds up no other event."""
        while True:
            udn, location, ip = self.fetches.get()
            try:
                description = parse_description(fetch(location))
            except Exception as failure:  # Whatever a device may send.
                print('network-places:', location, failure, file=sys.stderr, flush=True)
                description = None
            self.GLib.idle_add(self.fetched, udn, location, ip, description)

    def fetched(self, udn, location, ip, description):
        if self.upnp.get(udn, ('',))[0] != location:
            return False  # Gone or moved meanwhile.
        now = time.monotonic()
        if not description:
            if len(self.failed) >= MAX_UPNP:
                self.failed.clear()
            self.failed[location] = now + SEARCH_INTERVAL
        elif self.registry.add(('upnp', udn), upnp_service(description, location, ip), now, self.upnp[udn][1]):
            self.changed()
        return False

    def address_event(self, *_):
        while True:
            try:
                self.netlink.recv(65536)
            except BlockingIOError:
                break
            except OSError:
                break  # Overrun: the addresses are read anew anyway.
        if not self.address_timer:
            self.address_timer = self.GLib.timeout_add_seconds(2, self.addresses_changed)
        return True

    def addresses_changed(self):
        self.address_timer = None
        if interfaces() != self.addresses:
            # Another network, perhaps: what does not answer again goes.
            now = time.monotonic()
            for udn in list(self.upnp):
                self.registry.remove(('upnp', udn), now)
            self.upnp.clear()
            self.failed.clear()
            self.join()
            self.search()
            self.schedule_expiry()
        return False

    # mDNS, through Avahi's D-Bus API

    def start_avahi(self):
        Gio = self.Gio
        self.browsers, self.resolving = set(), set()
        try:
            self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        except self.GLib.Error as failure:
            print('network-places: no system bus, so no mDNS:', failure, file=sys.stderr, flush=True)
            return
        self.bus.signal_subscribe('org.freedesktop.Avahi', 'org.freedesktop.Avahi.ServiceBrowser', None, None,
                                  None, Gio.DBusSignalFlags.NONE, self.browser_signal)
        Gio.bus_watch_name_on_connection(self.bus, 'org.freedesktop.Avahi', Gio.BusNameWatcherFlags.NONE,
                                         self.avahi_appeared, self.avahi_vanished)

    def avahi_call(self, path, interface, method, signature, arguments, done=None):
        parameters = self.GLib.Variant(signature, arguments) if signature else None
        self.bus.call('org.freedesktop.Avahi', path, 'org.freedesktop.Avahi.' + interface, method, parameters,
                      None, self.Gio.DBusCallFlags.NONE, 10000, None, done, None)

    def avahi_appeared(self, *_):
        for kind_of in SERVICE_TYPES:
            self.browse(kind_of)

    def browse(self, kind_of):
        def prepared(bus, result, _):
            try:
                path = bus.call_finish(result).unpack()[0]
            except self.GLib.Error as failure:
                print('network-places: no browser for', kind_of, failure, file=sys.stderr, flush=True)
                return
            # Prepared first and started now, so that no signal is missed.
            self.browsers.add(path)
            self.avahi_call(path, 'ServiceBrowser', 'Start', None, None)

        # Any interface, IPv4.
        self.avahi_call('/', 'Server2', 'ServiceBrowserPrepare', '(iissu)', (-1, 0, kind_of, '', 0), prepared)

    def avahi_vanished(self, *_):
        now = time.monotonic()
        self.browsers.clear()
        self.resolving.clear()
        for key in list(self.registry.services):
            if key[0] == 'mdns':
                self.registry.remove(key, now)
        self.schedule_expiry()

    def browser_signal(self, bus, sender, path, interface, signal_name, parameters):
        if path not in self.browsers or signal_name not in ('ItemNew', 'ItemRemove'):
            return
        index, protocol, name, kind_of, domain, flags = parameters.unpack()
        key = ('mdns', index, protocol, name, kind_of, domain)
        if signal_name == 'ItemRemove':
            self.resolving.discard(key)
            self.registry.remove(key, time.monotonic())
            self.schedule_expiry()
            return
        if flags & (8 | 16):  # AVAHI_LOOKUP_RESULT_LOCAL, _OUR_OWN: this machine.
            return
        if len(self.resolving) >= MAX_SERVICES:
            return
        self.resolving.add(key)

        def resolved(bus, result, _):
            if key not in self.resolving:
                return  # Removed meanwhile.
            self.resolving.discard(key)
            try:
                values = bus.call_finish(result)
            except self.GLib.Error as failure:
                print('network-places: not resolved:', repr(name), kind_of, failure, file=sys.stderr, flush=True)
                return
            host, ip, port = (values.get_child_value(i).unpack() for i in (5, 7, 8))
            txt = values.get_child_value(9).unpack()
            service = mdns_service(kind_of, name, host, ip, port, txt)
            if service and self.registry.add(key, service, time.monotonic()):
                self.changed()
            elif service:
                self.schedule_expiry()  # Back within the grace period.

        self.avahi_call('/', 'Server', 'ResolveService', '(iisssiu)',
                        (index, protocol, name, kind_of, domain, 0, 0), resolved)

    def run(self):
        for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            self.GLib.unix_signal_add(self.GLib.PRIORITY_HIGH, number, lambda: self.loop.quit() or True)
        self.loop.run()


def options(text):
    """session.json's "network_places" object: show, limit and grace."""
    given = json.loads(text) if text else {}
    if not isinstance(given, dict) or set(given) - {'show', 'limit', 'grace'}:
        raise SystemExit('network_places takes only "show", "limit" and "grace"')
    show, limit, grace = given.get('show', list(KINDS)), given.get('limit', 30), given.get('grace', 60)
    if not isinstance(show, list) or any(kind not in KINDS for kind in show):
        raise SystemExit(f'network_places "show" is a list of {", ".join(KINDS)}')
    if not isinstance(limit, int) or limit < 0 or not isinstance(grace, (int, float)) or grace < 0:
        raise SystemExit('network_places "limit" and "grace" are numbers, 0 or more')
    return tuple(show), limit, grace


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', type=Path, help='the dedicated XP prefix')
    parser.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    parser.add_argument('--options', help='JSON: "show" (kinds), "limit" (shortcuts), "grace" (seconds)')
    parser.add_argument('--remove', action='store_true', help='remove the shortcuts made here and end')
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    if not (prefix / 'xp-shell.json').is_file():
        raise SystemExit('Expected a dedicated XP prefix')
    show, limit, grace = options(args.options)
    folder = Folder(prefix, args.wine)
    removed = folder.remove_all()
    if removed:
        print(f'network-places: {removed} removed', flush=True)
    if args.remove:
        return
    if not (prefix / 'drive_c/xp/bin/iexplore.exe').is_file():
        raise SystemExit('XP Explorer\'s additions are missing (prefix/extras.py)')
    terminal = None
    if shutil.which('ssh'):
        terminal = next((t for t in ('x-terminal-emulator', 'xterm') if shutil.which(t)), None)
    try:
        Discovery(folder, terminal, show, limit, grace).run()
    finally:
        folder.remove_all()


if __name__ == '__main__':
    main()
