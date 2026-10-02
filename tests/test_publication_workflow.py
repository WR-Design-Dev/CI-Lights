"""Exercise the real release shell steps with GitHub responses, without network access."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


def workflow_script(name):
    lines = (ROOT / '.github/workflows/firmware.yml').read_text(encoding='utf-8').splitlines()
    start = lines.index('      - name: ' + name)
    start = lines.index('        run: |', start) + 1
    body = []
    for line in lines[start:]:
        if line and not line.startswith('          '):
            break
        body.append(line[10:])
    return '\n'.join(body)


FAKE_GH = '''
gh() {
  if [[ "$1" == api ]]; then
    if [[ "$2" == */git/ref/heads/main ]]; then
      printf '%s\\n' "$MOCK_MAIN_SHA"
    elif [[ "$2" == */releases/latest ]]; then
      echo v1.0.1000
    else
      return 99
    fi
  elif [[ "$1" == release && "$2" == view ]]; then
    return 1
  elif [[ "$1" == release && "$2" == create ]]; then
    if [[ "$MOCK_CREATE" == success ]]; then
      echo 'release created'
    else
      echo "$MOCK_CREATE" >&2
      return 1
    fi
  else
    return 99
  fi
}
'''


class PublicationWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name == 'nt':
            git = shutil.which('git.exe')
            cls.bash = Path(git).parent.parent / 'bin/bash.exe' if git else None
        else:
            cls.bash = shutil.which('bash')
        if not cls.bash or not Path(cls.bash).exists():
            raise unittest.SkipTest('Bash or Git for Windows is required')

    def run_step(self, name, advanced=False, error='success'):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'release').mkdir()
            (root / 'release/CI-Lights.bin').write_bytes(b'firmware')
            env = dict(os.environ, GH_REPO='WR-Design-Dev/CI-Lights', GITHUB_SHA='a' * 40,
                       VERSION='1.0.1011', GITHUB_OUTPUT='step-output.txt',
                       MOCK_MAIN_SHA=('b' if advanced else 'a') * 40, MOCK_CREATE=error)
            result = subprocess.run([str(self.bash), '--noprofile', '--norc', '-e', '-o', 'pipefail',
                                     '-c', FAKE_GH + workflow_script(name)], cwd=root, env=env,
                                    capture_output=True, text=True, encoding='utf-8')
            output = root / 'step-output.txt'
            return result, output.read_text(encoding='utf-8') if output.exists() else ''

    def test_superseded_commit_is_skipped_successfully(self):
        result, output = self.run_step('Refuse an older publication', advanced=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.strip(), 'publish=false')

    def test_current_commit_can_publish(self):
        result, output = self.run_step('Refuse an older publication')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.strip(), 'publish=true')
        result, output = self.run_step('Publish signed release')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.strip(), 'publish=true')

    def test_push_during_release_creation_skips_pages(self):
        result, output = self.run_step('Publish signed release', advanced=True,
                                       error='HTTP 403: Resource not accessible by integration')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.strip(), 'publish=false')

    def test_permission_error_for_current_commit_still_fails(self):
        result, output = self.run_step('Publish signed release',
                                       error='HTTP 403: Resource not accessible by integration')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('publish=true', output)

    def test_other_errors_are_not_hidden_when_main_advances(self):
        result, output = self.run_step('Publish signed release', advanced=True, error='network timeout')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('publish=true', output)


if __name__ == '__main__':
    unittest.main()
