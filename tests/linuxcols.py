#!/usr/bin/env python3
"""The plain C part of XP Explorer's Linux columns
(xp-explorer/helpers/linuxcols.c), built with the host's gcc: no Wine, no X.

  tests/linuxcols.py

The permissions are compared with "ls -ld" for files of every type and special
bit this user can make or find, then the owner and group names with the pwd and
grp modules, the paths the columns leave alone, and where the Details section
may wrap a path.
"""
import grp
import os
from pathlib import Path
import pwd
import socket
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "%s"

static char *slurp(const char *path)
{
    static char text[1 << 20];
    FILE *file = fopen(path, "r");
    size_t read = file ? fread(text, 1, sizeof(text) - 1, file) : 0;
    text[read] = 0;
    if (file) fclose(file);
    return text;
}

int main(int argc, char **argv)
{
    char out[4096];
    if (argc == 3 && !strcmp(argv[1], "mode"))
    {
        format_mode((unsigned int)strtoul(argv[2], NULL, 8), out);
        puts(out);
    }
    else if (argc == 4 && !strcmp(argv[1], "name"))
    {
        if (!find_name(slurp(argv[2]), (unsigned int)strtoul(argv[3], NULL, 10), out, sizeof(out))) return 1;
        puts(out);
    }
    else if (argc == 3 && !strcmp(argv[1], "wrap"))
    {
        unsigned int run = 0, previous = 0;
        for (const char *c = argv[2]; *c; previous = (unsigned char)*c++)
        {
            putchar(*c);
            if (wraps_after((unsigned char)*c, previous, &run)) putchar('|');
        }
        putchar('\n');
    }
    else if (argc == 3 && !strcmp(argv[1], "skip"))
        puts(skip_path(argv[2]) ? "skip" : "keep");
    else return 2;
    return 0;
}
'''

failures = 0
checks = 0


def check(what, got, expected):
    global failures, checks
    checks += 1
    if got != expected:
        failures += 1
        print(f'FAILED {what}: {got!r}, expected {expected!r}')


def main():
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        source = temp / 'harness.c'
        source.write_text(HARNESS % (ROOT / 'xp-explorer/helpers/linuxcols.c'))
        program = temp / 'harness'
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2', '-o', str(program),
                        str(source)], check=True)

        def run(*arguments):
            result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True)
            return result.stdout.rstrip('\n') if result.returncode == 0 else None

        def mode(path):
            expected = subprocess.run(['ls', '-ld', str(path)], capture_output=True, text=True, check=True,
                                      env=dict(os.environ, LC_ALL='C')).stdout.split()[0]
            # ls appends "." (SELinux) or "+" (ACL); the column shows the mode alone.
            check(f'mode of {path}', run('mode', oct(os.lstat(path).st_mode)[2:]), expected[:10])

        # Every permission bit, with and without the x under each special bit.
        plain = temp / 'file'
        plain.touch()
        directory = temp / 'directory'
        directory.mkdir()
        for bits in (0o000, 0o644, 0o755, 0o421, 0o124, 0o4755, 0o4644, 0o2755, 0o2745, 0o1777, 0o1776,
                     0o7777, 0o7000):
            for path in (plain, directory):
                path.chmod(bits)
                mode(path)
        directory.chmod(0o755)
        link = temp / 'link'
        link.symlink_to('file')
        mode(link)
        fifo = temp / 'fifo'
        os.mkfifo(fifo)
        mode(fifo)
        listener = socket.socket(socket.AF_UNIX)
        listener.bind(str(temp / 'socket'))
        mode(temp / 'socket')
        listener.close()
        found = set()
        for path in sorted(Path('/dev').iterdir()):
            kind = stat.S_IFMT(path.lstat().st_mode)
            if kind in (stat.S_IFCHR, stat.S_IFBLK) and kind not in found:
                found.add(kind)
                mode(path)
        for path in ('/usr/bin/passwd', '/tmp', '/'):
            if os.path.exists(path):
                mode(path)

        for user in pwd.getpwall():
            if user.pw_name in Path('/etc/passwd').read_text():
                check(f'user {user.pw_uid}', run('name', '/etc/passwd', user.pw_uid),
                      pwd.getpwuid(user.pw_uid).pw_name)
        for group in grp.getgrall():
            if group.gr_name in Path('/etc/group').read_text():
                check(f'group {group.gr_gid}', run('name', '/etc/group', group.gr_gid),
                      grp.getgrgid(group.gr_gid).gr_name)
        names = temp / 'passwd'
        names.write_text('# comment:x:5:\n+nis::6:\nbroken line\nnoid:x:\nword:x:abc:\n'
                         'long:x:70000:70000::/:/bin/sh\nlast:x:7')
        check('no such ID', run('name', names, 4242), None)
        check('a comment', run('name', names, 5), None)
        check('an NIS line', run('name', names, 6), None)
        check('a large ID', run('name', names, 70000), 'long')
        check('a cut off line', run('name', names, 7), None)

        for path, expected in (('/proc/1/status', 'skip'), ('/PROC/self', 'skip'), ('/proc', 'keep'),
                               ('/procedures/x', 'keep'), ('/run/user/1000/gvfs', 'skip'),
                               ('/run/user/1000/gvfs/sftp:host=x/y', 'skip'), ('/Run/User/0/GVFS/x', 'skip'),
                               ('/run/user/1000/gvfsd', 'keep'), ('/run/user/1000/bus', 'keep'),
                               ('/run/user/gvfs', 'keep'), ('/run/user/', 'keep'), ('/usr/proc/x', 'keep'),
                               ('', 'keep')):
            check(f'skip {path}', run('skip', path), expected)

        for path, expected in (
                ('/usr/bin/7z', '/|usr/|bin/|7z'),
                ('/home/u/.local/x.so', '/|home/|u/|.local/|x.|so'),
                ('/i/usa__2296_x86fre.pro_whistler.iso', '/|i/|usa_|_|2296_|x86fre.|pro_|whistler.|iso'),
                ('/abcdefghijklmnopqrstuvwxyz0123456789', '/|abcdefghijklmn|opqrstuvwxyz01|23456789'),
                ('/a b c d e f g h i j k l', '/|a b c d e f g h i j k l'),
                ('/x/a-b', '/|x/|a-|b')):
            check(f'wrap {path}', run('wrap', path), expected)

    print(f'{checks} checks, {failures} failed')
    sys.exit(failures != 0)


if __name__ == '__main__':
    main()
