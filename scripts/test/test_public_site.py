import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "check_public_site", Path(__file__).resolve().parents[1] / "check_public_site.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class PublicSiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in checker.REQUIRED:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("runtime placeholder")

    def test_public_runtime_and_key_input_are_allowed(self):
        (self.root / "scripts/ai-ui.mjs").write_text(
            "const prompt = 'Enter your API key'; const property = 'apiKey';")
        self.assertEqual(checker.check_site(self.root), [])

    def test_private_package_is_blocked_without_echoing_credentials(self):
        fake_key = "sk-proj-" + "example" * 8
        (self.root / "local-key.json").write_text('{"apiKey":"' + fake_key + '"}')
        (self.root / "scripts/ai-ui.mjs").write_text(
            "async function loadLocalKey() { return fetch('local-key.json'); }")
        problems = checker.check_site(self.root)
        self.assertTrue(any("Private/configuration" in message for message in problems))
        self.assertTrue(any("embedded API credential" in message for message in problems))
        self.assertTrue(any("Private key-loading" in message for message in problems))
        self.assertNotIn(fake_key, "\n".join(problems))

    def test_missing_runtime_file_prevents_deployment(self):
        (self.root / "wasm/engine.wasm").unlink()
        self.assertTrue(any("Missing runtime" in p for p in checker.check_site(self.root)))


if __name__ == "__main__":
    unittest.main()
