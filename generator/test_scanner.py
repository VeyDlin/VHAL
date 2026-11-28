import os
import tempfile
import unittest

from scanner import scan_directory


def _write(path: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("")


def _collect_paths(node: dict) -> list[str]:
    paths: list[str] = [node["path"]]
    for child in node.get("children", []):
        paths.extend(_collect_paths(child))
    return paths


class ScannerTests(unittest.TestCase):
    def test_skips_test_folders_and_keeps_tool_folders(self) -> None:
        with tempfile.TemporaryDirectory() as vhal_root, tempfile.TemporaryDirectory() as docs_root:
            _write(os.path.join(vhal_root, "Module", ".test", "Foo.h"))
            _write(os.path.join(vhal_root, "Module", ".tool", "Bar.h"))
            _write(os.path.join(vhal_root, "Module", "Baz.h"))

            node: dict | None = scan_directory(vhal_root, docs_root, "Module")

            self.assertIsNotNone(node)
            paths: list[str] = _collect_paths(node)
            self.assertIn("Module/Baz.h", paths)
            self.assertIn("Module/.tool/Bar.h", paths)
            self.assertFalse(any(".test" in p for p in paths))

    def test_excludes_test_segment_only_not_substrings(self) -> None:
        with tempfile.TemporaryDirectory() as vhal_root, tempfile.TemporaryDirectory() as docs_root:
            _write(os.path.join(vhal_root, "Module", "latest", "Latest.h"))
            _write(os.path.join(vhal_root, "Module", ".tester", "Tester.h"))

            node: dict | None = scan_directory(vhal_root, docs_root, "Module")

            self.assertIsNotNone(node)
            paths: list[str] = _collect_paths(node)
            self.assertIn("Module/latest/Latest.h", paths)
            self.assertIn("Module/.tester/Tester.h", paths)
