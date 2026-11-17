from html.parser import HTMLParser
import importlib.util
from pathlib import Path
import tempfile
import unittest
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('site_build', ROOT/'website/build.py')
site_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site_build)


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.targets = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for attr in ('href', 'src'):
            if attr in attrs:
                self.targets.append(attrs[attr])


class Website(unittest.TestCase):
    def test_project_subpath_local_assets_and_fragments(self):
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
            output = site_build.build('test-owner/test-project', folder)
            html = (output/'index.html').read_text()
            self.assertNotIn('@@', html)
            self.assertIn('https://github.com/test-owner/test-project', html)
            links = Links()
            links.feed(html)
            for link in links.targets:
                parts = urlsplit(link)
                if parts.scheme:
                    self.assertEqual(parts.scheme, 'https')
                elif parts.path:
                    self.assertFalse(parts.path.startswith('/'), link)
                    self.assertTrue((output/parts.path).is_file(), link)
                elif parts.fragment:
                    self.assertIn(parts.fragment, links.ids)
            self.assertIn('Physical board validation is pending', html)
            self.assertIn('Full peripheral and stress checks remain open', html)
            self.assertTrue((output/'.nojekyll').is_file())

    def test_repository_cannot_inject_html_and_output_cannot_overwrite_source(self):
        for repository in ('bad', 'x/y<script>', 'https://github.com/x/y', '../x', 'x/..'):
            with self.assertRaises(ValueError):
                site_build.build(repository)
        with self.assertRaises(ValueError):
            site_build.build('owner/repo', ROOT/'website')


if __name__ == '__main__':
    unittest.main()
