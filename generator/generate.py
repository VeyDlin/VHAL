"""
Documentation generator — orchestrates scanner, cpp_parser, and readme_parser.

Builds a navigation tree and generates per-page JSON files.
- Directory pages: README from .wiki/
- File pages: API from the .h file + per-file docs from .wiki/
"""

import os
import re
import json

from scanner import build_navigation
from cpp_parser import parse_header
from readme_parser import parse_readme


def _strip_html(html: str) -> str:
    """Remove HTML tags and collapse whitespace."""
    text = re.sub(r'<[^>]+>', ' ', html)
    text = re.sub(r'\s+', ' ', text)
    return text.strip()


def _extract_symbols(api: list[dict]) -> str:
    """Extract symbol names, types, qualifiers from API data into a searchable string."""
    tokens: list[str] = []

    def _add_member(member: dict):
        kind = member.get('kind', '')
        name = member.get('name', '')
        if name:
            tokens.append(name)
        if kind == 'method':
            ret = member.get('returnType', '')
            if ret:
                tokens.append(ret)
            for q in member.get('qualifiers', []):
                tokens.append(q)
            for p in member.get('params', []):
                ptype = p.get('type', '')
                pname = p.get('name', '')
                if ptype:
                    tokens.append(ptype)
                if pname:
                    tokens.append(pname)
        elif kind == 'field':
            ftype = member.get('type', '')
            if ftype:
                tokens.append(ftype)
        elif kind == 'enum':
            tokens.extend(member.get('values', []))
        if 'members' in member:
            _walk_members(member['members'])

    def _walk_members(members: dict):
        for access in ('public', 'protected', 'private'):
            for member in members.get(access, []):
                _add_member(member)

    for file_data in api:
        for sym in file_data.get('symbols', []):
            name = sym.get('name', '')
            if name:
                tokens.append(name)
            template = sym.get('template', '')
            if template:
                tokens.append(template)
            for base in sym.get('bases', []):
                tokens.append(base)
            if 'members' in sym:
                _walk_members(sym['members'])

    return ' '.join(t for t in tokens if t)


def _build_search_index(all_pages: list[dict]) -> list[dict]:
    """Build search index entries from all pages."""
    index = []
    for page in all_pages:
        path = page['path']
        parts = path.replace('\\', '/').split('/')
        breadcrumb = ' > '.join(parts[:-1]) if len(parts) > 1 else ''

        index.append({
            'id': path.replace('/', '--'),
            'title': page['label'],
            'path': path,
            'breadcrumb': breadcrumb,
            'readme': _strip_html(page.get('readme', '')),
            'symbols': _extract_symbols(page.get('api', [])),
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
