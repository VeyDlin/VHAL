"""
Documentation generator — orchestrates scanner, cpp_parser, and readme_parser.

Builds a navigation tree and generates per-page JSON files.
- Directory pages: README from .wiki/
- File pages: API from the .h file + per-file docs from .wiki/
"""

import os
import json
import re
from html.parser import HTMLParser

from scanner import build_navigation
from cpp_parser import parse_header
from readme_parser import parse_readme


class SearchTextParser(HTMLParser):
    block_tags: frozenset[str] = frozenset({
        'div', 'p', 'pre', 'li', 'td', 'th', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
        'section', 'article', 'tr',
    })
    skipped_tags: frozenset[str] = frozenset({'script', 'style', 'noscript', 'button'})
    void_tags: frozenset[str] = frozenset({
        'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta',
        'param', 'source', 'track', 'wbr',
    })

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self.skip_stack: list[str] = []


    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if self.skip_stack:
            if tag not in self.void_tags:
                self.skip_stack.append(tag)
            return

        attributes: dict[str, str | None] = dict(attrs)
        classes: set[str] = set((attributes.get('class') or '').split())
        is_hidden: bool = 'hidden' in attributes
        is_aria_hidden: bool = (attributes.get('aria-hidden') or '').strip().lower() == 'true'
        is_anchor_control: bool = tag == 'a' and 'anchor-link' in classes

        if tag in self.skipped_tags or is_hidden or is_aria_hidden or is_anchor_control:
            if tag not in self.void_tags:
                self.skip_stack.append(tag)
            return

        if tag in self.block_tags or tag == 'br':
            self.parts.append(' ')


    def handle_endtag(self, tag: str) -> None:
        if self.skip_stack:
            if tag in self.skip_stack:
                matching_index: int = len(self.skip_stack) - 1 - self.skip_stack[::-1].index(tag)
                del self.skip_stack[matching_index:]
            return

        if tag in self.block_tags:
            self.parts.append(' ')


    def handle_data(self, data: str) -> None:
        if not self.skip_stack:
            self.parts.append(data)


def _strip_html(html: str) -> str:
    """Extract visible search text and collapse whitespace."""
    parser: SearchTextParser = SearchTextParser()
    parser.feed(html)
    parser.close()
    return ' '.join(''.join(parser.parts).split())


def _extract_symbols(api: list[dict]) -> tuple[str, list[dict]]:
    """Extract visible API words and compact structural origins."""
    tokens: list[str] = []
    origins: list[dict] = []
    word_count: int = 0

    def append_token(value: str, key: str) -> None:
        nonlocal word_count
        words: list[str] = re.findall(r'\S+', value)
        if not words:
            return
        if not origins or origins[-1]['key'] != key:
            origins.append({'start': word_count, 'key': key})
        tokens.append(value)
        word_count += len(words)

    def add_member(member: dict, key: str) -> None:
        kind = member.get('kind', '')
        name = member.get('name', '')
        if name:
            append_token(name, key)
        if kind == 'method':
            ret = member.get('returnType', '')
            if ret:
                append_token(ret, key)
            for q in member.get('qualifiers', []):
                if q in ('static', 'const', 'virtual', 'override', 'constexpr', 'noexcept'):
                    append_token(q, key)
            for p in member.get('params', []):
                ptype = p.get('type', '')
                pname = p.get('name', '')
                if ptype:
                    append_token(ptype, key)
                if pname:
                    append_token(pname, key)
        elif kind == 'field':
            ftype = member.get('type', '')
            if ftype:
                append_token(ftype, key)
        elif kind == 'enum':
            for value in member.get('values', []):
                append_token(value, key)
        if 'members' in member:
            walk_members(member['members'], key)

    def walk_members(members: dict, parent_key: str) -> None:
        for access in ('public', 'protected', 'private'):
            for index, member in enumerate(members.get(access, [])):
                add_member(member, f'{parent_key}/{access}/{index}')

    for file_index, file_data in enumerate(api):
        for symbol_index, sym in enumerate(file_data.get('symbols', [])):
            key: str = f'f{file_index}/s{symbol_index}'
            name = sym.get('name', '')
            if name:
                append_token(name, key)
            template = sym.get('template', '')
            if template:
                append_token(template, key)
            for base in sym.get('bases', []):
                append_token(base, key)
            if 'members' in sym:
                walk_members(sym['members'], key)

    return ' '.join(tokens), origins


def _build_search_index(all_pages: list[dict]) -> list[dict]:
    """Build search index entries from all pages."""
    index = []
    for page in all_pages:
        path = page['path']
        parts = path.replace('\\', '/').split('/')
        breadcrumb = ' > '.join(parts[:-1]) if len(parts) > 1 else ''
        symbols, symbol_origins = _extract_symbols(page.get('api', []))

        index.append({
            'id': path.replace('/', '--'),
            'title': page['label'],
            'path': path,
            'breadcrumb': breadcrumb,
            'readme': _strip_html(page.get('readme', '')),
            'symbols': symbols,
            'symbolOrigins': symbol_origins,
            'hasReadme': bool(page.get('readme', '')),
        })
    return index


def _collect_pages(node: dict, vhal_root: str, docs_root: str) -> list[dict]:
    """Recursively collect page data from tree nodes."""
    pages = []
    node_path = node.get("path")
    node_type = node.get("type", "directory")

    if node_path is None:
        for child in node.get("children", []):
            pages.extend(_collect_pages(child, vhal_root, docs_root))
        return pages

    if node_type == "file":
        has_headers = node.get("hasHeaders", False)
        dir_rel = os.path.dirname(node_path)
        fname = os.path.basename(node_path)
        docs_dir = os.path.join(docs_root, dir_rel)

        readme_html = ""
        api = []

        if has_headers:
            # Source file node: .h with optional per-file docs
            abs_file = os.path.join(vhal_root, node_path)
            header_docs = node.get("headerDocs", [])
            for md_name in header_docs:
                md_file = os.path.join(docs_dir, md_name)
                if os.path.isfile(md_file):
                    readme_html = parse_readme(md_file)
            if os.path.isfile(abs_file):
                header_data = parse_header(abs_file)
                api.append(header_data)
        else:
            # Docs-only file node: .md in wiki
            docs_file = node.get("docsFile")
            if docs_file:
                md_file = os.path.join(docs_dir, docs_file)
            else:
                md_file = os.path.join(docs_dir, fname + ".md")
            if os.path.isfile(md_file):
                readme_html = parse_readme(md_file)

        pages.append({
            "path": node_path,
            "label": node["label"],
            "readme": readme_html,
            "api": api,
        })

    elif node_type == "directory":
        has_readme = node.get("hasReadme", False)

        if has_readme:
            docs_dir = os.path.join(docs_root, node_path)
            readme_file = os.path.join(docs_dir, "README.md")
            readme_html = ""
            if os.path.isfile(readme_file):
                readme_html = parse_readme(readme_file)

            pages.append({
                "path": node_path,
                "label": node["label"],
                "readme": readme_html,
                "api": [],
            })

    for child in node.get("children", []):
        pages.extend(_collect_pages(child, vhal_root, docs_root))

    return pages


def generate(vhal_root: str, output_dir: str, docs_root: str) -> None:
    """Generate navigation.json and per-page JSON files."""
    vhal_root = os.path.abspath(vhal_root)
    output_dir = os.path.abspath(output_dir)
    docs_root = os.path.abspath(docs_root)

    navigation = build_navigation(vhal_root, docs_root)

    pages_dir = os.path.join(output_dir, "pages")
    os.makedirs(pages_dir, exist_ok=True)

    # Clean old pages
    for f in os.listdir(pages_dir):
        if f.endswith(".json"):
            os.remove(os.path.join(pages_dir, f))

    nav_path = os.path.join(output_dir, "navigation.json")
    with open(nav_path, "w", encoding="utf-8") as f:
        json.dump(navigation, f, indent=2, ensure_ascii=False)

    all_pages = []
    for node in navigation:
        all_pages.extend(_collect_pages(node, vhal_root, docs_root))

    # Generate home page from root README.md
    root_readme = os.path.join(vhal_root, "README.md")
    if os.path.isfile(root_readme):
        home_page = {
            "path": "__home",
            "label": "VHAL",
            "readme": parse_readme(root_readme),
            "api": [],
        }
        home_path = os.path.join(pages_dir, "__home.json")
        with open(home_path, "w", encoding="utf-8") as f:
            json.dump(home_page, f, indent=2, ensure_ascii=False)

    for page in all_pages:
        slug = page["path"].replace("/", "--")
        page_path = os.path.join(pages_dir, f"{slug}.json")
        with open(page_path, "w", encoding="utf-8") as f:
            json.dump(page, f, indent=2, ensure_ascii=False)

    # Generate search index
    search_index = _build_search_index(all_pages)
    search_path = os.path.join(output_dir, "search-index.json")
    with open(search_path, "w", encoding="utf-8") as f:
        json.dump(search_index, f, ensure_ascii=False)

    print(f"Generated {len(all_pages)} pages, search-index.json, and navigation.json in {output_dir}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate VHAL documentation")
    parser.add_argument("--vhal-root", required=True, help="Path to VHAL root directory")
    parser.add_argument("--output", required=True, help="Output directory for generated files")
    parser.add_argument("--docs-root", default=None, help="Path to docs directory (default: {vhal-root}/.wiki)")
    args = parser.parse_args()

    docs_root = args.docs_root or os.path.join(args.vhal_root, ".wiki")
    generate(args.vhal_root, args.output, docs_root)
