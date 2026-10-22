# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DockerHandoff(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.producer = self.base / 'producer with spaces'
        (self.producer / 'src').mkdir(parents=True)
        (self.producer / 'src/iqs7211e.c').write_text('/* selected input */\n')
        self.companion = self.base / 'filter companion'
        (self.companion / 'src').mkdir(parents=True)
        (self.companion / 'src/input_processor_suppress.c').write_text('/* selected companion */\n')
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.calls = self.base / 'docker-args.json'
        docker = self.bin / 'docker'
        docker.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
pathlib.Path(os.environ['DOCKER_CALLS']).write_text(json.dumps(sys.argv[1:]))
sys.exit(int(os.environ.get('DOCKER_EXIT', '0')))
''')
        docker.chmod(0o755)
        west = self.bin / 'west'
        west.write_text('#!/bin/sh\necho called >> "$WEST_CALLS"\nexit 86\n')
        west.chmod(0o755)
        self.west_calls = self.base / 'west-calls'
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        DOCKER_CALLS=str(self.calls), WEST_CALLS=str(self.west_calls),
                        TMPDIR=str(self.base), PYTHONDONTWRITEBYTECODE='1',
                        ZMK_TEST_SUPPRESS_ROOT=str(self.companion))
        for key in ('ZMK_TEST_IQS_ROOT', 'ZMK_TEST_REQUIRE_IQS', 'ZMK_TEST_OUTPUT',
                    'ZMK_TEST_OFFLINE_WORKSPACE', 'DOCKER_EXIT'):
            self.env.pop(key, None)

    def run_entry(self, name='tests/run-integration-docker.sh', **env):
        return subprocess.run(['bash', str(ROOT / name), 'upstream'],
                              env=dict(self.env, **env), capture_output=True,
                              text=True, timeout=10)

    def test_actual_producer_and_required_mode_cross_docker_readonly(self):
        output = self.base / 'results parent' / 'fresh run'
        result = self.run_entry(ZMK_TEST_IQS_ROOT=str(self.producer),
                                ZMK_TEST_REQUIRE_IQS='1', ZMK_TEST_OUTPUT=str(output))
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.calls.read_text())
        self.assertIn(f'{self.producer}:/iqs:ro', args)
        self.assertIn(f'{self.companion}:/suppress:ro', args)
        self.assertIn('ZMK_TEST_SUPPRESS_ROOT=/suppress', args)
        self.assertIn('ZMK_TEST_IQS_ROOT=/iqs', args)
        self.assertIn('ZMK_TEST_REQUIRE_IQS=1', args)
        self.assertIn(f'{output.parent}:/results', args)
        self.assertIn('ZMK_TEST_OUTPUT=/results/fresh run', args)
        self.assertEqual(args[-3:], ['/bin/bash', '/src/tests/integration/run.sh', 'upstream'])
        self.assertFalse(output.exists())

    def test_required_missing_producer_fails_before_docker_or_west(self):
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            with self.subTest(entry=entry):
                result = self.run_entry(entry, ZMK_TEST_REQUIRE_IQS='1')
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('IQS producer required', result.stderr)
                self.assertFalse(self.calls.exists())
                self.assertFalse(self.west_calls.exists())

    def test_invalid_producer_fails_before_docker_or_west(self):
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            for path in (self.base / 'missing', self.base):
                with self.subTest(entry=entry, path=str(path)):
                    result = self.run_entry(entry, ZMK_TEST_IQS_ROOT=str(path),
                                            ZMK_TEST_REQUIRE_IQS='1')
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn('IQS producer source missing', result.stderr)
                    self.assertFalse(self.calls.exists())
                    self.assertFalse(self.west_calls.exists())

    def test_invalid_required_mode_is_not_a_skipped_success(self):
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            with self.subTest(entry=entry):
                result = self.run_entry(entry, ZMK_TEST_REQUIRE_IQS='yes')
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('must be 0 or 1', result.stderr)
                self.assertFalse(self.calls.exists())
                self.assertFalse(self.west_calls.exists())

    def test_existing_linked_dangling_and_relative_outputs_rejected(self):
        output = self.base / 'old-results'
        output.mkdir()
        sentinel = output / 'sentinel'
        sentinel.write_text('preserve ordered prior evidence\n')
        linked = self.base / 'link'
        linked.symlink_to(output, target_is_directory=True)
        dangling = self.base / 'dangling'
        dangling.symlink_to(self.base / 'absent', target_is_directory=True)
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            for path in (output, linked, dangling, Path('relative-results')):
                with self.subTest(entry=entry, path=str(path)):
                    result = self.run_entry(entry, ZMK_TEST_OUTPUT=str(path))
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn('fresh absolute path', result.stderr)
                    self.assertFalse(self.calls.exists())
                    self.assertFalse(self.west_calls.exists())
                    self.assertEqual(sentinel.read_text(), 'preserve ordered prior evidence\n')

    def test_online_failure_retains_requested_fresh_output(self):
        output = self.base / 'retained-failure'
        result = self.run_entry('tests/integration/run.sh',
                                ZMK_TEST_OUTPUT=str(output),
                                ZMK_TEST_REQUIRE_IQS='1',
                                ZMK_TEST_IQS_ROOT=str(self.producer))
        self.assertEqual(result.returncode, 86, result.stdout + result.stderr)
        self.assertTrue(self.west_calls.is_file())
        self.assertEqual((output / 'config/west.yml').read_bytes(),
                         (ROOT / 'tests/integration/config/upstream/west.yml').read_bytes())

    def test_missing_companion_fails_before_docker_or_west(self):
        for entry in ('tests/run-integration-docker.sh', 'tests/integration/run.sh'):
            result = self.run_entry(entry, ZMK_TEST_SUPPRESS_ROOT=str(self.base / 'absent'))
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn('explicit suppression companion required', result.stderr)
            self.assertFalse(self.calls.exists())
            self.assertFalse(self.west_calls.exists())

    def test_docker_failure_is_propagated(self):
        result = self.run_entry(ZMK_TEST_IQS_ROOT=str(self.producer),
                                ZMK_TEST_REQUIRE_IQS='1', DOCKER_EXIT='37')
        self.assertEqual(result.returncode, 37, result.stderr)

    def test_optional_manual_run_preserves_explicit_not_run_selection(self):
        result = self.run_entry()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = json.loads(self.calls.read_text())
        self.assertFalse(any(arg.startswith('ZMK_TEST_IQS_ROOT=') for arg in args))
        self.assertIn('ZMK_TEST_REQUIRE_IQS=0', args)


if __name__ == '__main__':
    unittest.main()
