#!/usr/bin/env python3
"""Regression checks for archive builds after an interrupted XNU pass."""
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from unixnu import kernel
from unixnu.util import Error


class KernelBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        (root / 'scripts').mkdir()
        (root / 'scripts/build-xnu-4k-baseline.sh').touch()
        objects = root / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
        startup = objects / 'osfmk/DEVELOPMENT/startup.o'
        startup.parent.mkdir(parents=True)
        startup.with_suffix('.o.json').write_text('{}')
        self.objects = [startup, startup.with_name('arm_init.o')]
        (objects / 'link.filelist').write_text(''.join(str(p) + '\n' for p in self.objects))
        self.ctx = SimpleNamespace(tree=root, jobs=2, logs=root / 'logs', need_config=lambda: {})

    def invoke(self, run, archives):
        with patch.object(kernel, 'tool'), patch.object(kernel, 'run', side_effect=run), \
                patch.object(kernel, 'build_archives', side_effect=archives):
            kernel.build(self.ctx, {})

    def test_stale_records_restore_objects_before_archives(self):
        events = []

        def run(command, **kwargs):
            if kwargs['env'].get('XNU_BOOTSTRAP') == '1':
                events.append('bootstrap')
                for obj in self.objects:
                    obj.touch()
                raise Error('expected link failure without archives')
            events.append('full')

        self.invoke(run, lambda *args: events.append('archives'))
        self.assertEqual(events, ['bootstrap', 'archives', 'full'])

    def test_incomplete_bootstrap_does_not_run_archives(self):
        def run(command, **kwargs):
            self.objects[0].touch()
            raise Error('compile failed before the second object')

        with self.assertRaisesRegex(Error, 'compile failed'):
            self.invoke(run, lambda *args: self.fail('archives need all link objects'))

    def test_complete_inputs_skip_bootstrap(self):
        for obj in self.objects:
            obj.touch()
        events = []
        self.invoke(lambda *args, **kwargs: events.append('full'),
                    lambda *args: events.append('archives'))
        self.assertEqual(events, ['archives', 'full'])


if __name__ == '__main__':
    unittest.main()
