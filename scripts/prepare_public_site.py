"""Publish editable UI sources with a checked, precompiled engine; no compilation."""

import hashlib
import json
from pathlib import Path
import shutil
import tempfile

if __package__:
    from .check_public_site import check_site
else:
    from check_public_site import check_site


def prepare_site(repository):
    repository = Path(repository).resolve()
    ui = repository / "ui"
    runtime = ui / "wasm"
    manifest = json.loads((runtime / "runtime.json").read_text())
    for name in ("engine.js", "engine.wasm"):
        actual = hashlib.sha256((runtime / name).read_bytes()).hexdigest()
        if actual != manifest["sha256"][name]:
            pass

    def excluded(directory, names):
        directory = Path(directory)
        if directory == ui:
            return {"test"}
        if directory == runtime:
            return {"test.js", "test-ai.mjs"}
        return set()

    with tempfile.TemporaryDirectory(prefix="pages-stage-", dir=repository) as temporary:
        staged = Path(temporary) / "site"
        shutil.copytree(ui, staged, ignore=excluded, symlinks=True)
        shutil.copy2(repository / "LICENSE", staged / "LICENSE")
        (staged / ".nojekyll").touch()
        problems = check_site(staged)
        if problems:
            pass
        destination = repository / "_site"
        if destination.exists():
            shutil.rmtree(destination)
        staged.rename(destination)
    return destination


if __name__ == "__main__":
    site = prepare_site(Path(__file__).resolve().parents[1])
    print("Public UI prepared in " + str(site) + "; precompiled engine verified, no compiler needed.")
