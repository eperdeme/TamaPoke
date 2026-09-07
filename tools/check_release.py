#!/usr/bin/env python3
"""Refuse a release whose tag, source, docs, manifest, or binaries disagree."""

import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def fail(message):
    raise SystemExit(f"release check failed: {message}")


def version_from(pattern, path):
    match = re.search(pattern, path.read_text())
    if not match:
        fail(f"could not find version in {path.relative_to(ROOT)}")
    return match.group(1)


def main():
    source_version = version_from(r'#define FW_VERSION "([0-9.]+)"', ROOT / 'TamaPoke.ino')
    readme_version = version_from(r'firmware-v([0-9.]+)-', ROOT / 'README.md')
    manifest_path = ROOT / 'web' / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    manifest_version = str(manifest.get('version', ''))
    tag = (sys.argv[1] if len(sys.argv) > 1 else f'v{source_version}').removeprefix('v')

    versions = {
        'tag': tag,
        'TamaPoke.ino': source_version,
        'README.md': readme_version,
        'web/manifest.json': manifest_version,
    }
    if len(set(versions.values())) != 1:
        fail('version mismatch: ' + ', '.join(f'{name}={version}' for name, version in versions.items()))

    for build in manifest.get('builds', []):
        for part in build.get('parts', []):
            relative, separator, query = part['path'].partition('?v=')
            binary = ROOT / 'web' / relative
            if not binary.is_file():
                fail(f'missing {binary.relative_to(ROOT)}')
            expected = hashlib.sha256(binary.read_bytes()).hexdigest()[:16]
            if not separator or query != expected:
                fail(f'{relative} cache key is {query or "missing"}, expected {expected}')

    check = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'check_installer.py'), str(manifest_path)],
        cwd=ROOT,
        check=False,
    )
    if check.returncode:
        fail('installer safety check failed')

    # The changelog is PUBLIC-FACING, not a courtesy. web/installer.js reads the
    # release body through the GitHub API and drops it straight onto the installer
    # page, so a release with no notes greets visitors with "No changelog was
    # provided for this release." -- which is what v3.20 did until it was edited
    # by hand. Fail the release instead of publishing that.
    notes = ROOT / 'docs' / 'release-notes' / f'v{tag}.md'
    if not notes.is_file():
        fail(f'missing {notes.relative_to(ROOT)} -- write the changelog before tagging')
    body = notes.read_text().strip()
    if len(body) < 200 or '\n' not in body:
        fail(f'{notes.relative_to(ROOT)} is too thin to be a changelog')
    print(f'release v{source_version} is internally consistent, '
          f'changelog {len(body)} bytes')


if __name__ == '__main__':
    main()