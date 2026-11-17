"""Run each user-manual session against the generated native light engine."""
from pathlib import Path
import re
import subprocess

from native import build

ROOT = Path(__file__).resolve().parents[2]
MANUAL = ROOT / 'esp8266/docs/USER_MANUAL.md'


def check():
    text = MANUAL.read_text()
    sessions = re.findall(
        r'^```mathsmith\n(.*?)^```\n(?:(?!^```mathsmith).)*?^```text\n(.*?)^```',
        text, re.MULTILINE | re.DOTALL)
    count = len(re.findall(r'^```mathsmith$', text, re.MULTILINE))
    if not count or len(sessions) != count:
        raise SystemExit('Each mathsmith example needs a following text result block')
    binary = build()
    for number, (commands, expected) in enumerate(sessions, 1):
        if not commands.startswith(':reset\n'):
            raise SystemExit(f'Example {number}: must start with a fresh workspace')
        result = subprocess.run([str(binary)], input=commands, text=True,
                                capture_output=True, timeout=20, check=True)
        output = result.stdout
        if re.search(r'(?i)\berror:|line discarded|Unknown platform command', output):
            raise SystemExit(f'Example {number} failed:\n{output}')
        # Compare whole output lines, ignoring only spacing used to align columns.
        lines = [' '.join(line.split()) for line in output.splitlines()]
        position = 0
        for line in expected.splitlines():
            wanted = ' '.join(line.split())
            if not wanted:
                continue
            try:
                position = lines.index(wanted, position) + 1
            except ValueError:
                raise SystemExit(f'Example {number}: missing result {line!r}\n{output}')
    print(f'PASS: {count} user-manual sessions match the native light engine.')
    print('Native logic check only; no board opened or hardware result inferred.')


if __name__ == '__main__':
    check()
