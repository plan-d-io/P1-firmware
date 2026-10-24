#!/usr/bin/env python3
"""Refuse a push that would publish private or working-method content.

The forbidden-word list is `_local/prepush_words.txt` and the forbidden-path
list is `_local/prepush_paths.txt` (both untracked). If either file is
missing, the check refuses the push.

Install the hook on another computer, from the repository root:

    python tools/prepush_check.py --install-hook
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORDS_PATH = ROOT / "_local" / "prepush_words.txt"
PATHS_PATH = ROOT / "_local" / "prepush_paths.txt"
SECRET_CHECK = ROOT / "tools" / "secret_check.py"
HOOK_PATH = ROOT / ".git" / "hooks" / "pre-push"


def git(*args: str, check: bool = True) -> str:
    r = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if check and r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SystemExit(r.returncode or 1)
    return r.stdout


def load_list(path: Path, kind: str) -> list[str]:
    rel = path.relative_to(ROOT).as_posix()
    if not path.is_file():
        print(
            f"prepush_check: missing {rel}; "
            f"refusing the push so the private {kind} list cannot be skipped.",
            file=sys.stderr,
        )
        raise SystemExit(1)
    items: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        items.append(s)
    if not items:
        print(f"prepush_check: {rel} has no {kind}; refusing.",
              file=sys.stderr)
        raise SystemExit(1)
    return items


def load_words() -> list[str]:
    return load_list(WORDS_PATH, "words")


def load_paths() -> list[str]:
    return load_list(PATHS_PATH, "paths")


def is_forbidden_path(path: str, forbidden: list[str]) -> bool:
    if path.startswith("_local/"):
        return True
    return path in forbidden


def commits_in_range(remote_sha: str, local_sha: str) -> list[str]:
    if local_sha == "0" * 40:
        return []  # deleting a ref
    if remote_sha == "0" * 40:
        out = git("rev-list", "--max-count=200", local_sha)
        return [ln for ln in out.splitlines() if ln]
    return [ln for ln in git("rev-list", f"{remote_sha}..{local_sha}").splitlines() if ln]


def check_tracked_paths(forbidden: list[str]) -> list[str]:
    hits: list[str] = []
    listed = git("ls-files").splitlines()
    for path in listed:
        if is_forbidden_path(path, forbidden):
            hits.append(f"tracked path {path}")
    return hits


def check_commits(commits: list[str], words: list[str],
                  forbidden: list[str]) -> list[str]:
    hits: list[str] = []
    if not commits:
        return hits
    for h in commits:
        msg = git("log", "-1", "--format=%B", h)
        for w in words:
            if w in msg:
                hits.append(f"{h[:12]} commit message contains {w!r}")
        files = git("diff-tree", "--no-commit-id", "-r", "--name-only", h)
        names = [n for n in files.splitlines() if n]
        if not names:
            names = [n for n in git("ls-tree", "-r", "--name-only", h).splitlines() if n]
        for path in names:
            if is_forbidden_path(path, forbidden):
                hits.append(f"{h[:12]} tracks {path}")
            try:
                blob = subprocess.check_output(
                    ["git", "show", f"{h}:{path}"],
                    cwd=ROOT,
                    stderr=subprocess.DEVNULL,
                )
            except subprocess.CalledProcessError:
                continue
            try:
                text = blob.decode("utf-8")
            except UnicodeDecodeError:
                text = blob.decode("utf-8", "replace")
            for w in words:
                if w.encode("utf-8") in blob or w in text:
                    hits.append(f"{h[:12]}:{path} contains {w!r}")
    return hits


def provision_needles() -> list[tuple[str, bytes]]:
    """Distinctive provision values, never printed. Skip short flags."""
    path = ROOT / "_local" / "provision.txt"
    if not path.is_file():
        return []
    out: list[tuple[str, bytes]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s.lower().startswith("set "):
            continue
        parts = s.split(None, 2)
        if len(parts) != 3:
            continue
        key, val = parts[1], parts[2]
        if len(val) < 6:
            continue
        if val.lower() in {"true", "false"}:
            continue
        out.append((key, val.encode("utf-8")))
    return out


def check_secrets(commits: list[str]) -> list[str]:
    hits: list[str] = []
    sys.path.insert(0, str(ROOT / "tools"))
    from secret_check import load_pair  # noqa: WPS433

    key, secret = load_pair()
    extra = provision_needles()
    if not commits:
        return hits
    for h in commits:
        files = git("ls-tree", "-r", "--name-only", h).splitlines()
        for path in files:
            try:
                blob = subprocess.check_output(
                    ["git", "show", f"{h}:{path}"],
                    cwd=ROOT,
                    stderr=subprocess.DEVNULL,
                )
            except subprocess.CalledProcessError:
                continue
            if key in blob:
                hits.append(f"{h[:12]}:{path} EnergieID key: found")
            if secret in blob:
                hits.append(f"{h[:12]}:{path} EnergieID secret: found")
            for name, needle in extra:
                if needle in blob:
                    hits.append(f"{h[:12]}:{path} provision {name}: found")
    return hits


def install_hook() -> int:
    HOOK_PATH.parent.mkdir(parents=True, exist_ok=True)
    py = sys.executable.replace("\\", "/")
    script = (ROOT / "tools" / "prepush_check.py").as_posix()
    body = (
        "#!/bin/sh\n"
        "# Installed by tools/prepush_check.py --install-hook\n"
        f'exec "{py}" "{script}" --hook\n'
    )
    HOOK_PATH.write_text(body, encoding="utf-8", newline="\n")
    try:
        os.chmod(HOOK_PATH, 0o755)
    except OSError:
        pass
    print(f"installed {HOOK_PATH}")
    return 0


def evaluate(commits: list[str]) -> int:
    words = load_words()
    forbidden = load_paths()
    hits = check_tracked_paths(forbidden)
    hits.extend(check_commits(commits, words, forbidden))
    hits.extend(check_secrets(commits))
    if hits:
        print("prepush_check: refusing:", file=sys.stderr)
        for h in hits:
            print(f"  {h}", file=sys.stderr)
        return 1
    print("prepush_check: ok")
    return 0


def run_hook() -> int:
    ranges: list[tuple[str, str]] = []
    stdin = sys.stdin.read().splitlines()
    if not stdin:
        head = git("rev-parse", "HEAD").strip()
        ranges.append(("0" * 40, head))
    for line in stdin:
        parts = line.split()
        if len(parts) < 4:
            continue
        _lref, local_sha, _rref, remote_sha = parts[:4]
        ranges.append((remote_sha, local_sha))
    commits: list[str] = []
    seen: set[str] = set()
    for remote_sha, local_sha in ranges:
        for h in commits_in_range(remote_sha, local_sha):
            if h not in seen:
                seen.add(h)
                commits.append(h)
    return evaluate(commits)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--install-hook", action="store_true")
    ap.add_argument("--hook", action="store_true",
                    help="read pre-push refs from stdin")
    ap.add_argument("--range", dest="rev_range",
                    help="git rev range to check, e.g. main..HEAD")
    args = ap.parse_args()
    if args.install_hook:
        return install_hook()
    if args.rev_range:
        commits = [ln for ln in git("rev-list", args.rev_range).splitlines() if ln]
        return evaluate(commits)
    return run_hook()


if __name__ == "__main__":
    raise SystemExit(main())
