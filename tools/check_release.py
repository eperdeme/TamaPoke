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


def git(*args):
    """Runs git, returning stdout stripped, or None if the command failed."""
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def check_tag_on_main(tag):
    """A release is cut from main, never from a branch tip -- see CLAUDE.md § Git.

    Documented-only conventions rot: v3.18 and v3.22 were both tagged on a branch
    tip, and the Pages site serves web/ from main, so a release that never lands
    there leaves every visitor on the old firmware while the tag claims otherwise.
    """
    ref = f'v{tag}'
    commit = git('rev-parse', '--verify', f'{ref}^{{commit}}')
    if commit is None:
        print(f'note: {ref} does not exist yet, skipping the main-ancestry check')
        return
    for candidate in ('refs/remotes/origin/main', 'refs/heads/main'):
        main = git('rev-parse', '--verify', f'{candidate}^{{commit}}')
        if main is not None:
            break
    if main is None:
        fail('cannot find main to check the tag against; fetch origin and retry')
    if subprocess.run(['git', 'merge-base', '--is-ancestor', commit, main],
                      cwd=ROOT, check=False).returncode != 0:
        fail(f'{ref} ({commit[:9]}) is not on main ({main[:9]}). '
             'Merge the branch into main and push it before tagging -- see CLAUDE.md.')
    print(f'{ref} is on main')


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

    # The JS cache keys, on the same terms as the firmware parts above. Pages sits
    # behind a CDN, so a fresh installer.js paired with a STALE savefile.js is a
    # real failure -- and it would look like the save backup quietly behaving the
    # way it did before. build_web.sh computes both; this refuses a release where
    # somebody has hand-edited one out of step.
    for source, pattern in (
        (ROOT / 'web' / 'installer.js', r"from '\./savefile\.js\?v=([0-9a-f]+)'"),
        (ROOT / 'web' / 'index.html', r'src="installer\.js\?v=([0-9a-f]+)"'),
    ):
        target = ROOT / 'web' / ('savefile.js' if source.name == 'installer.js' else 'installer.js')
        match = re.search(pattern, source.read_text())
        if not match:
            fail(f'{source.relative_to(ROOT)} has no cache key for {target.name}; run tools/build_web.sh')
        expected = hashlib.sha256(target.read_bytes()).hexdigest()[:16]
        if match.group(1) != expected:
            fail(f'{source.relative_to(ROOT)} points at {target.name}?v={match.group(1)}, '
                 f'expected {expected}; run tools/build_web.sh')

    # The changelog is PUBLIC-FACING, not a courtesy. web/installer.js reads the
    # release body through the GitHub API and drops it straight onto the installer
    # page, so a release with no notes greets visitors with "No changelog was
    # provided for this release." -- which is what v3.20 did until it was edited
    # by hand. Fail the release instead of publishing that.
    check_tag_on_main(tag)

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