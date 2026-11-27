#!/usr/bin/env python3
"""Mount a disc image with UDisks2, for XP's "Mount" on .iso files.

  iso_mount.py IMAGE RESULT

linux-launch.exe --mount-iso (session/linux-launch.c) runs it as C:\\xp\\bin\\iso_mount.py
and waits for the file RESULT: Wine gives a Windows program neither the exit
status of a Unix program nor its output through a pipe. RESULT is written in one
piece, either

  ok            or    error
  /dev/loopN          what went wrong
  MOUNTPOINT
  new or old          mounted now, or already before

The image is attached to a loop device read-only and mounted by udisksd, so Wine's
mountmgr.sys, which follows UDisks2, makes it a CD drive. An image that already is
on a loop device is not attached a second time.
"""
import fcntl
import glob
import os
import re
import stat
import subprocess
import sys
import tempfile
import time

DISC_TYPES = ('iso9660', 'udf')


class Refused(Exception):
    pass


def udisksctl(*arguments):
    """Run udisksctl, return its output. Without questions: there is nobody to answer them."""
    env = dict(os.environ, LC_ALL='C.UTF-8')
    try:
        done = subprocess.run(['udisksctl', arguments[0], '--no-user-interaction', *arguments[1:]], env=env,
                              stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace', timeout=25)
    except FileNotFoundError:
        raise Refused('UDisks2 is not installed (the Debian package udisks2).') from None
    except subprocess.TimeoutExpired:
        raise Refused('UDisks2 did not answer.') from None
    if done.returncode:
        # "Error ...: GDBus.Error:org.freedesktop.UDisks2.Error.Failed: reason"
        text = re.sub(r'GDBus\.Error:[\w.]+: ', '', done.stderr.strip() or done.stdout.strip())
        raise Refused(text or f'udisksctl {arguments[0]} failed.')
    return done.stdout


def loop_device(image, sys_block='/sys/block'):
    """The loop device that already holds IMAGE, or None."""
    for path in sorted(glob.glob(f'{sys_block}/loop*/loop/backing_file'),
                       key=lambda p: int(re.findall(r'loop(\d+)/loop/', p)[-1])):
        try:
            with open(path, 'rb') as f:
                backing = os.fsdecode(f.read().removesuffix(b'\n'))
            if backing == image or os.path.samefile(backing, image):
                return '/dev/' + path.split('/')[-3]
        except OSError:
            continue
    return None


def mounted(device, mounts='/proc/self/mounts'):
    """(mount point, file system type) of DEVICE, or None."""
    with open(mounts, 'rb') as f:
        for line in f:
            fields = line.split()
            if len(fields) > 2 and os.fsdecode(fields[0]) == device:
                # Spaces and the like are written as \ooo.
                point = re.sub(rb'\\([0-7]{3})', lambda m: bytes([int(m[1], 8)]), fields[1])
                return os.fsdecode(point), os.fsdecode(fields[2])
    return None


def mount_device(device):
    # Straight after loop-setup udisksd may not have looked at the disc yet.
    for attempt in range(5):
        try:
            udisksctl('mount', '-b', device)
            return
        except Refused as error:
            if 'not a mountable filesystem' not in str(error):
                raise
            if attempt == 4:
                raise Refused('It is not a disc image (ISO 9660 or UDF).') from None
            time.sleep(0.2)


def mount(image):
    """(device, mount point, 'new' or 'old') of IMAGE, mounted now or already."""
    image = os.path.realpath(image)
    try:
        if not stat.S_ISREG(os.stat(image).st_mode):
            raise Refused('It is not a file.')
    except OSError as error:
        raise Refused(f'{error.strerror}.') from None
    device = loop_device(image)
    made = False
    if device:
        found = mounted(device)
        if found:
            return device, found[0], 'old'
    else:
        # No partitions: a hybrid image (a Linux live CD) would become several drives.
        output = udisksctl('loop-setup', '-r', '--no-partition-scan', '-f', image)
        match = re.search(r'(/dev/loop\d+)\.?\s*$', output)
        if not match:
            raise Refused(f'udisksctl did not name a loop device: {output.strip()}')
        device, made = match[1], True
    try:
        mount_device(device)
        found = mounted(device)
        if not found:
            raise Refused(f'{device} was mounted, but is not in the list of mounts.')
        if found[1] not in DISC_TYPES:
            udisksctl('unmount', '-b', device)
            raise Refused('It is not a disc image (ISO 9660 or UDF).')
    except Refused:
        if made:
            try:
                udisksctl('loop-delete', '-b', device)
            except Refused as error:
                print(f'{device} stays: {error}', file=sys.stderr)
        raise
    return device, found[0], 'new'


def locked_mount(image):
    """mount(), one at a time: two double clicks must not make two drives."""
    runtime = os.environ.get('XDG_RUNTIME_DIR') or tempfile.gettempdir()
    with open(os.path.join(runtime, f'win4wine-iso-{os.getuid()}.lock'), 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return mount(image)


def main(image, result):
    try:
        text = 'ok\n{}\n{}\n{}\n'.format(*locked_mount(image))
    except Refused as error:
        text = f'error\n{error}\n'
    except Exception as error:  # The caller waits for a result whatever happens.
        text = f'error\n{type(error).__name__}: {error}\n'
    print(f'{image}: {text}', end='', file=sys.stderr)
    temporary = result + '.new'
    with open(temporary, 'wb') as f:
        f.write(text.encode('utf-8', 'surrogateescape'))
    os.replace(temporary, result)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
