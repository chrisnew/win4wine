#!/usr/bin/env python3
"""Check session/network_places.py without Wine, Avahi or a network: what is
made of announcements, hostile ones included, and which files are touched.

  tests/network_places.py
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'session'))
import network_places as np  # noqa: E402


def check_names():
    assert np.clean('  FRITZ!Box 7590 / "Cable"  ') == 'FRITZ!Box 7590 Cable'
    assert np.clean('..\\..\\evil\t\n\0name.') == 'evil name'
    assert np.clean('a‮gnp.exe b\ud800') == 'agnp.exe b'
    assert np.clean('CON') == '_CON' and np.clean('nul.local') == '_nul.local' and np.clean('Contoso') == 'Contoso'
    assert np.clean(' . ') == '' and len(np.clean('x' * 500)) == 60
    assert np.unreachable('Anna\'s "PC" & more') == ['--unreachable', 'ANNA-S-PC-MORE']
    assert np.unreachable('プリンタ') == ['--unreachable', 'COMPUTER']


def check_urls():
    assert np.make_url('http', '192.168.1.2', 80, '/') == 'http://192.168.1.2/'
    assert np.make_url('https', '192.168.1.2', 8443, 'a b"c\\') == 'https://192.168.1.2:8443/a%20b%22c%5C'
    # A path cannot name another host, and nothing but http and https passes.
    assert np.make_url('http', '192.168.1.2', 80, '//evil.example/x') == 'http://192.168.1.2/evil.example/x'
    assert np.make_url('file', '192.168.1.2', 80, '/etc/passwd') == ''
    assert np.make_url('http', 'evil.example', 80, '/') == '' and np.make_url('http', '127.0.0.1', 80, '/') == ''
    # Nor an announced address out on the internet, or none of a single device.
    for ip in ('8.8.8.8', '224.0.0.251', '0.0.0.0', '255.255.255.255'):
        assert np.address(ip) is None, ip
    assert np.address('169.254.3.4') and np.address('172.16.0.1')
    assert np.mdns_service('_http._tcp', 'x', 'x.local', '93.184.216.34', 80, []) is None
    assert np.make_url('http', '192.168.1.2', 0, '/') == '' and np.make_url('http', '192.168.1.2', 70000, '/') == ''
    assert np.rebase_url('http://evil.example:81/p?q=1', '10.0.0.1') == 'http://10.0.0.1:81/p?q=1'
    assert np.rebase_url('../ui', '10.0.0.1', 'http://10.0.0.1:5000/d/desc.xml') == 'http://10.0.0.1:5000/ui'
    assert np.rebase_url('javascript:alert(1)', '10.0.0.1') == ''
    assert np.rebase_url('http://10.0.0.1:99999/', '10.0.0.1') == ''
    for url in (np.make_url('http', '10.0.0.1', 80, '/ \r\n"\\\udcff'),):
        assert url and all(c not in url for c in ' "\\\r\n\t'), url


def check_mdns():
    web = np.mdns_service('_http._tcp', 'Admin', 'nas.local.', '10.0.0.5', 8080, [b'path=/admin', b'u=x'])
    assert web['url'] == 'http://10.0.0.5:8080/admin' and web['host'] == 'nas', web
    printer = np.mdns_service('_ipp._tcp', 'Laser', 'laser.local', '10.0.0.6', 631,
                              [b'adminurl=http://laser.local./status'])
    assert printer['url'] == 'http://10.0.0.6/status', printer
    assert np.mdns_service('_ipp._tcp', 'L', 'l.local', '10.0.0.6', 631, [])['url'] == ''
    assert np.mdns_service('_ssh._tcp', 'x', 'x.local', 'fe80::1', 22, []) is None
    assert np.mdns_service('_airplay._tcp', 'x', 'x.local', '10.0.0.1', 1, []) is None
    assert np.mdns_service('_ssh._tcp', 'x', 'x.local', '10.0.0.1', 0, []) is None


def message(first, **headers):
    return ('\r\n'.join([first, *(f'{k.replace("_", "-")}: {v}' for k, v in headers.items())]) + '\r\n\r\n').encode()


def check_ssdp():
    location = 'http://192.168.1.1:49000/desc.xml'
    alive = message('NOTIFY * HTTP/1.1', NT='upnp:rootdevice', NTS='ssdp:alive', USN='uuid:1::upnp:rootdevice',
                    LOCATION=location, CACHE_CONTROL='max-age = 120')
    assert np.ssdp_event(alive, '192.168.1.1') == ('alive', 'uuid:1', location, 120)
    found = message('HTTP/1.1 200 OK', ST='upnp:rootdevice', USN='uuid:1::upnp:rootdevice', LOCATION=location)
    assert np.ssdp_event(found, '192.168.1.1') == ('alive', 'uuid:1', location, 1800)
    # A description elsewhere, an address on the internet and other devices' types are ignored.
    assert np.ssdp_event(alive, '192.168.1.66') is None
    assert np.ssdp_event(alive.replace(b'192.168.1.1:', b'8.8.8.8:'), '8.8.8.8') is None
    for bad in (b'http://192.168.1.1:x/', b'file:///etc/passwd', b'http://user@evil/', b'http://[', b'http://192.168.1.1/\x01'):
        assert np.ssdp_event(alive.replace(location.encode(), bad), '192.168.1.1') is None, bad
    assert np.ssdp_event(alive.replace(b'NT: upnp:rootdevice', b'NT: uuid:1'), '192.168.1.1') is None
    bye = message('NOTIFY * HTTP/1.1', NT='uuid:1', NTS='ssdp:byebye', USN='uuid:1')
    assert np.ssdp_event(bye, '192.168.1.1') == ('gone', 'uuid:1')
    assert np.ssdp_event(message('M-SEARCH * HTTP/1.1', ST='ssdp:all'), '192.168.1.1') is None
    assert np.ssdp_event(b'\xff\x00 nonsense', '192.168.1.1') is None


DESCRIPTION = b'''<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><device>
 <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
 <friendlyName>Home &amp; Router</friendlyName><presentationURL>http://router.example/</presentationURL>
 <deviceList><device><friendlyName>Inner</friendlyName><presentationURL>/inner</presentationURL></device></deviceList>
</device></root>'''


def check_description():
    found = np.parse_description(DESCRIPTION)
    assert found['friendlyName'] == 'Home & Router' and found['presentationURL'] == 'http://router.example/', found
    router = np.upnp_service(found, 'http://192.168.1.1:49000/desc.xml', '192.168.1.1')
    assert (router['kind'], router['url'], router['icon']) == ('web', 'http://192.168.1.1/', 'router'), router
    bomb = (b'<?xml version="1.0"?><!DOCTYPE root [<!ENTITY a "aaaaaaaaaa"><!ENTITY b "&a;&a;&a;&a;">]>'
            b'<root><device><friendlyName>&b;</friendlyName></device></root>')
    external = (b'<!DOCTYPE root [<!ENTITY x SYSTEM "file:///etc/passwd">]>'
                b'<root><device><friendlyName>&x;</friendlyName></device></root>')
    for bad in (bomb, external, bomb.decode().replace('?>', ' encoding="utf-16"?>').encode('utf-16'),
                b'<root>', b'', b'<html></html>', b'<a>' * 100 + b'</a>' * 100,
                b'<?xml version="1.0" encoding="no-such"?><root/>'):
        assert np.parse_description(bad) is None, bad[:80]


def check_entries():
    router = np.upnp_service(np.parse_description(DESCRIPTION), 'http://192.168.1.1/d.xml', '192.168.1.1')
    services = [
        router,
        # The router's page over mDNS too, and over https: one shortcut, with the UPnP name.
        np.mdns_service('_http._tcp', 'web', 'gw.local', '192.168.1.1', 80, []),
        np.mdns_service('_https._tcp', 'web', 'gw.local', '192.168.1.1', 443, []),
        np.mdns_service('_workstation._tcp', 'gw [aa]', 'gw.local', '192.168.1.1', 9, []),
        # One computer on two addresses, announcing a second page and SSH on both.
        np.mdns_service('_http._tcp', 'a', 'nas.local', '192.168.1.5', 80, []),
        np.mdns_service('_http._tcp', 'b', 'nas.local', '192.168.1.5', 8080, []),
        np.mdns_service('_ssh._tcp', 'nas', 'nas.local', '192.168.1.5', 22, []),
        np.mdns_service('_ssh._tcp', 'nas', 'nas.local', '192.168.2.5', 22, []),
        np.mdns_service('_smb._tcp', 'nas', 'nas.local', '192.168.2.5', 445, []),
        np.mdns_service('_ipp._tcp', 'Laser', 'laser.local', '192.168.1.6', 631, []),
        np.mdns_service('_http._tcp', 'Laser', 'laser.local', '192.168.1.6', 80, []),
        np.mdns_service('_workstation._tcp', '-oProxyCommand=x [bb]', '-oProxyCommand=x.local', '192.168.1.7', 9, []),
    ]
    entries = {e['name']: e for e in np.shortcuts(services, 'x-terminal-emulator')}
    assert list(entries) == ['Web page on Home & Router', 'Web page on nas', 'Web page on nas (8080)',
                             'Printer on laser', 'SSH on nas', '-oProxyCommand=x'], list(entries)
    assert entries['Web page on Home & Router']['arguments'] == 'http://192.168.1.1/'
    assert entries['Web page on nas (8080)']['arguments'] == 'http://192.168.1.5:8080/'
    assert entries['Printer on laser']['arguments'] == 'http://192.168.1.6/'
    assert entries['Printer on laser']['target'] == np.BROWSER
    ssh = entries['SSH on nas']
    assert ssh['arguments'].startswith('x-terminal-emulator -T "SSH on nas" -e sh -c ') and \
        ssh['arguments'].endswith(' ssh -p 22 -- 192.168.1.5'), ssh
    assert entries['-oProxyCommand=x']['arguments'] == '--unreachable OPROXYCOMMAND-X'
    # Without a terminal no SSH; a printer without a page says it is not accessible.
    entries = np.shortcuts([services[6], services[9]], None)
    assert [(e['name'], e['arguments']) for e in entries] == \
        [('Printer on laser', '--unreachable LASER'), ('nas', '--unreachable NAS')], entries
    assert [e['kind'] for e in np.shortcuts(services, 'xterm', show=('printer', 'ssh'))] == ['printer', 'ssh']
    assert [e['kind'] for e in np.shortcuts(services, 'xterm', limit=2)] == ['web', 'web']


def check_registry():
    registry = np.Registry(60)
    assert registry.add('a', {'n': 1}, 0) and not registry.add('a', {'n': 1}, 1)
    assert registry.add('u', {'n': 2}, 0, lifetime=100) and registry.deadline() == 160
    registry.remove('a', 10)
    assert registry.deadline() == 70 and registry.expire(69) == []
    registry.add('a', {'n': 1}, 50)   # Back within the grace period.
    assert registry.deadline() == 160
    registry.refresh('u', 150, 100)
    assert registry.expire(200) == [] and registry.expire(310) == ['u']
    registry.remove('a', 400)
    registry.remove('a', 430)         # Removed again: the first time counts.
    assert registry.expire(460) == ['a'] and registry.deadline() is None
    for number in range(np.MAX_SERVICES + 5):
        registry.add(number, {}, 0)
    assert len(registry.services) == np.MAX_SERVICES


def check_folder(base):
    prefix = base / 'prefix'
    (prefix / 'drive_c').mkdir(parents=True)
    folder = np.Folder(prefix, 'wine', user='me')
    folder.links.mkdir(parents=True)
    mine = folder.links / 'web page on NAS.lnk'
    mine.write_text('the user\'s own\n')
    runs, fail = [], set()

    def wine(command, input, **_):
        """linux-links.exe's stand-in."""
        out = []
        runs.append(input.splitlines())
        for line in runs[-1]:
            fields = line.split('\t')
            assert len(fields) == 6, fields
            name = fields[0].rsplit('\\', 1)[1]
            if name not in fail:
                (folder.links / name).write_text(line)
            out.append(('fail 0x1 ' if name in fail else 'ok ') + fields[0])
        return subprocess.CompletedProcess(command, bool(fail), '\n'.join(out) + '\n', '')

    def entry(name, url):
        return dict(name=name, kind='web', icon=13, target=np.BROWSER, arguments=url, description='d \tx')

    one, two = entry('Web page on nas', 'http://10.0.0.1/'), entry('Two', 'http://10.0.0.2/')
    with mock.patch.object(np.subprocess, 'run', wine), mock.patch.object(folder, 'install_launcher'):
        assert folder.sync([one, two]) == (2, 0)
        assert sorted(folder.load()) == ['Two.lnk', 'Web page on nas (2).lnk']
        assert mine.read_text() == 'the user\'s own\n'
        assert folder.sync([one, two]) == (0, 0) and len(runs) == 1
        two = entry('Two', 'http://10.0.0.3/')
        assert folder.sync([two]) == (1, 1) and len(runs[-1]) == 1
        assert not (folder.links / 'Web page on nas (2).lnk').exists() and mine.exists()
        # A failed write is tried again with the next change.
        fail.add('Three.lnk')
        three = entry('Three', 'http://10.0.0.4/')
        assert folder.sync([two, three]) == (0, 0)
        fail.clear()
        assert folder.sync([two, three]) == (1, 0)
        (folder.links / 'Two.lnk').unlink()
        assert folder.sync([two, three]) == (1, 0)
    # An interrupted write: the file is listed already, and the next start removes it.
    with mock.patch.object(np.subprocess, 'run', side_effect=subprocess.TimeoutExpired('wine', 1)), \
            mock.patch.object(folder, 'install_launcher'):
        try:
            folder.sync([two, three, entry('Four', 'http://10.0.0.5/')])
        except subprocess.TimeoutExpired:
            pass
    (folder.links / 'Four.lnk').write_text('half\n')
    # A list that names a file elsewhere is not followed.
    state = json.loads(folder.state_file.read_text())
    state['links']['../outside.lnk'] = ''
    folder.state_file.write_text(json.dumps(state))
    outside = folder.links.parent / 'outside.lnk'
    outside.write_text('kept\n')
    assert folder.remove_all() == 3
    assert sorted(p.name for p in folder.links.iterdir()) == ['web page on NAS.lnk'] and outside.exists()


def check_options():
    assert np.options(None) == (np.KINDS, 30, 60)
    assert np.options('{"show": ["web"], "limit": 5, "grace": 1.5}') == (('web',), 5, 1.5)
    for bad in ('[]', '{"shows": []}', '{"show": ["x"]}', '{"limit": -1}', '{"grace": "1"}'):
        try:
            np.options(bad)
        except SystemExit:
            continue
        raise AssertionError(bad)


def main():
    for check in (check_names, check_urls, check_mdns, check_ssdp, check_description, check_entries,
                  check_registry, check_options):
        check()
    with tempfile.TemporaryDirectory() as directory:
        check_folder(Path(directory))
    print('network-places: PASS')


if __name__ == '__main__':
    main()
