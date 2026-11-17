"""Build the static project site; no external packages or network needed."""
import argparse
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def build(repository, output=None):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Expected a GitHub owner/repository name')
    if repository.split('/')[1] in ('.', '..'):
        raise ValueError('Invalid repository name')
    output = Path(output or ROOT / 'build/github-pages').resolve()
    # Keep generated output outside source, even if given a wrong --output.
    if not output.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('Site output must be inside build/')
    output.mkdir(parents=True, exist_ok=True)
    html = (ROOT / 'website/index.html').read_text()
    html = html.replace('@@REPO_URL@@', 'https://github.com/' + repository)
    if '@@' in html:
        raise ValueError('Unresolved website placeholder')
    (output / 'index.html').write_text(html)
    (output / 'style.css').write_bytes((ROOT / 'website/style.css').read_bytes())
    (output / '.nojekyll').write_text('')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', default=os.environ.get('GITHUB_REPOSITORY', 'hgamoreira/mathsmith-os'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(build(args.repository, args.output))
