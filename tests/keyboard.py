#!/usr/bin/env python3
"""Check XKB detection with and without access to systemd-localed."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = spec_from_file_location('session_keyboard', Path(__file__).resolve().parents[1] / 'session/keyboard.py')
keyboard = module_from_spec(spec)
spec.loader.exec_module(keyboard)


class Keyboard(unittest.TestCase):
    def test_offline_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'keyboard'
            config.write_text('XKBMODEL="pc105"\nXKBLAYOUT="de,us" # comment\nXKBVARIANT=",intl"\n'
                              'XKBOPTIONS="compose:ralt"\nBACKSPACE="guess"\n')
            with patch.object(keyboard, 'DEBIAN_KEYBOARD', config), \
                    patch.object(subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, stdout='')):
                settings = keyboard.system()
        self.assertEqual(settings, {'model': 'pc105', 'layout': 'de,us', 'variant': ',intl',
                                    'options': 'compose:ralt'})
        self.assertEqual(keyboard.klids_for(settings), ['00000407', '00020409'])

    def test_live_layout_takes_priority(self):
        live = subprocess.CompletedProcess([], 0, stdout='  X11 Layout: gb\n  X11 Model: pc105\n')
        with patch.object(subprocess, 'run', return_value=live) as run:
            settings = keyboard.system()
        self.assertEqual(run.call_args.kwargs['env']['LC_ALL'], 'C')
        self.assertEqual(keyboard.klids_for(settings), ['00000809'])


if __name__ == '__main__':
    unittest.main()
