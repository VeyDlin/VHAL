import unittest
from pathlib import Path

from readme_parser import parse_readme


class ReadmeParserTests(unittest.TestCase):
    def test_documentation_links_use_the_site_base_path(self) -> None:
        fixture: Path = Path(__file__).parent / "fixtures" / "links.md"
        rendered: str = parse_readme(str(fixture))

        self.assertIn('href="/VHAL/docs/VHAL/VHAL_Config#uart"', rendered)
        self.assertIn('href="/VHAL/docs/VHAL/First_Project"', rendered)
        self.assertNotIn('/VHAL/VHAL/', rendered)


    def test_other_link_destinations_are_preserved(self) -> None:
        fixture: Path = Path(__file__).parent / "fixtures" / "links.md"
        rendered: str = parse_readme(str(fixture))

        self.assertIn('href="https://example.com/docs/reference"', rendered)
        self.assertIn('href="#configuration"', rendered)
        self.assertIn('href="./example.txt"', rendered)
        self.assertIn('href="//example.com/docs/reference"', rendered)
