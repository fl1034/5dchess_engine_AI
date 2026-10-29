import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from scripts.prepare_public_site import prepare_site
from scripts.check_public_site import REQUIRED


class PreparePublicSiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in REQUIRED:
            path = self.root / "ui" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("public runtime placeholder")
        self.runtime = self.root / "ui/wasm"
        checksums = {name: hashlib.sha256((self.runtime / name).read_bytes()).hexdigest()
                     for name in ("engine.js", "engine.wasm")}
        (self.runtime / "runtime.json").write_text(json.dumps({"sha256": checksums}))
        (self.root / "LICENSE").write_text("test license")

    def test_ui_edits_are_published_without_changing_the_engine(self):
        (self.root / "ui/index.html").write_text("Updated UI from WebStorm")
        (self.root / "ui/test").mkdir()
        (self.root / "ui/test/unit.mjs").write_text("test only")
        (self.runtime / "test.js").write_text("test only")
        site = prepare_site(self.root)
        self.assertEqual((site / "index.html").read_text(), "Updated UI from WebStorm")
        self.assertEqual((site / "wasm/engine.wasm").read_bytes(),
                         (self.runtime / "engine.wasm").read_bytes())
        self.assertFalse((site / "test").exists())
        self.assertFalse((site / "wasm/test.js").exists())
        self.assertTrue((site / "LICENSE").exists())
        self.assertTrue((site / ".nojekyll").exists())

    def test_damaged_engine_does_not_replace_a_working_site(self):
        site = prepare_site(self.root)
        previous = (site / "index.html").read_bytes()
        (self.runtime / "engine.wasm").write_bytes(b"damaged")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            prepare_site(self.root)
        self.assertEqual((site / "index.html").read_bytes(), previous)

    def test_private_key_is_rejected_before_replacing_a_working_site(self):
        site = prepare_site(self.root)
        previous = (site / "index.html").read_bytes()
        (self.root / "ui/local-key.json").write_text('{"apiKey":"private"}')
        with self.assertRaisesRegex(ValueError, "Private/configuration"):
            prepare_site(self.root)
        self.assertEqual((site / "index.html").read_bytes(), previous)

    def test_symlinks_cannot_pull_external_files_into_the_public_site(self):
        site = prepare_site(self.root)
        previous = (site / "index.html").read_bytes()
        external = self.root / "outside-ui.txt"
        external.write_text("not for publishing")
        (self.root / "ui/external.txt").symlink_to(external)
        with self.assertRaisesRegex(ValueError, "Symlink"):
            prepare_site(self.root)
        self.assertEqual((site / "index.html").read_bytes(), previous)


if __name__ == "__main__":
    unittest.main()
