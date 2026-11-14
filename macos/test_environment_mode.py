"""Focused regressions for environment isolation and safe process ownership."""
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
import supervisor


class EnvironmentModeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.build_patch = patch.object(supervisor, 'BUILD', Path(self.temporary.name))
        self.build_patch.start()
        self.addCleanup(self.build_patch.stop)
        self.addCleanup(self.temporary.cleanup)
        self.session = supervisor.Session(values=supervisor.preferences.DEFAULTS)

    def test_parent_injection_and_locale_overrides_are_not_inherited(self):
        with patch.dict(supervisor.os.environ, {'DYLD_INSERT_LIBRARIES': '/old/hook',
                'CP_EXECUTABLE_MAP': '/old/map', 'NODE_OPTIONS': '--require old',
                'LC_TIME': 'zh_CN.UTF-8', 'LANGUAGE': 'zh', 'PATH': '/bin'}, clear=True):
            env = self.session.environment()
        self.assertEqual(env['TZ'], 'America/Los_Angeles')
        self.assertEqual(env['LANG'], 'en_US.UTF-8')
        self.assertEqual(env['LC_ALL'], 'en_US.UTF-8')
        self.assertEqual(env['PATH'], '/bin')
        for key in ('DYLD_INSERT_LIBRARIES', 'CP_EXECUTABLE_MAP', 'NODE_OPTIONS', 'LC_TIME', 'LANGUAGE'):
            self.assertNotIn(key, env)

    def test_only_observed_descendants_belong_to_session(self):
        def item(pid, ppid, identity):
            return {'pid': pid, 'ppid': ppid, 'identity': identity,
                    'path': '/Applications/Claude.app/Contents/MacOS/Claude'}
        table = {10: item(10, 1, (1, 0)), 11: item(11, 10, (2, 0)),
                 12: item(12, 11, (3, 0)), 20: item(20, 1, (4, 0))}
        self.session.known[10] = (1, 0)
        with patch.object(supervisor, 'processes', return_value=table):
            self.assertEqual(set(self.session.owned()), {10, 11, 12})
        # A reparented child remains tracked, but a reused PID does not.
        table[11]['ppid'] = 1
        table[10]['identity'] = (99, 0)
        with patch.object(supervisor, 'processes', return_value=table):
            self.assertEqual(set(self.session.owned()), {11, 12})

    def test_signal_does_not_target_reused_pid(self):
        observed = {'pid': 10, 'identity': (1, 0)}
        with patch.object(supervisor, 'process_info', return_value={'pid': 10, 'identity': (2, 0)}), patch.object(supervisor.os, 'kill') as kill:
            self.session.signal_one(observed, supervisor.signal.SIGTERM)
        kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
