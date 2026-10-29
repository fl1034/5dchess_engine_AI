"""Reject private builds or embedded credentials before publishing static files."""

import re
import sys
from pathlib import Path

KEY_PATTERN = re.compile(rb"sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{20,}")
REQUIRED = ("index.html", "wasm/engine.js", "wasm/engine.wasm",
            "scripts/ai-ui.mjs", "scripts/ai-models.mjs")


def check_site(directory):
    root = Path(directory).resolve()
    problems = []
    for name in REQUIRED:
        if not (root / name).is_file():
            problems.append("Missing runtime file: " + name)
    for path in sorted(root.rglob("*")):
        relative = str(path.relative_to(root))
        if path.is_symlink():
            problems.append("Symlink is not allowed in public artifacts: " + relative)
            continue
        if not path.is_file():
            continue
        name = path.name.lower()
        if (name == "local-key.json" or name.endswith(".local.json")
                or name == ".env" or name.startswith(".env.")
                or name.endswith(".zip")):
            problems.append("Private/configuration archive file: " + relative)
        content = path.read_bytes()
        if KEY_PATTERN.search(content):
            problems.append("Possible embedded API credential in: " + relative)
        if path.suffix in (".js", ".mjs", ".html") and (
                b"loadLocalKey" in content or b"local-key.json" in content):
            problems.append("Private key-loading build detected in: " + relative)
    return problems


def main():
    if len(sys.argv) != 2:
        print("Usage: python3 scripts/check_public_site.py SITE_DIRECTORY")
        return 2
    problems = check_site(sys.argv[1])
    for problem in problems:
        print(problem)  # File names only; never print matched credentials.
    if problems:
        return 1
    print("Public site verified: runtime files present, no bundled key or private-build loader.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
