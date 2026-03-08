"""
README parser — converts README.md to HTML with raw code blocks for client-side highlighting.

Uses markdown-it-py for markdown parsing. Code blocks are output as
<pre><code class="language-xxx"> with raw text — Shiki highlights them on the client.
Headings get id anchors and a clickable link icon (like GitHub).
"""

import re
import html as html_module
from markdown_it import MarkdownIt


def _slugify(text: str) -> str:
    """Convert heading text to a URL-friendly slug (GitHub-style)."""
    text = text.lower().strip()
    text = re.sub(r"[^\w\s-]", "", text)
    text = re.sub(r"[\s]+", "-", text)
    return text.strip("-")


def _fence_renderer(self, tokens, idx, options, env):
    """Render fenced code blocks with language class for Shiki."""
    token = tokens[idx]
    code = html_module.escape(token.content)
    info = token.info.strip() if token.info else ""
    lang = info.split()[0] if info else ""
    lang_attr = f' class="language-{lang}"' if lang else ""
    return f"<pre><code{lang_attr}>{code}</code></pre>\n"


def _heading_open_renderer(self, tokens, idx, options, env):
    """Render heading open tag with id anchor and link icon."""
    token = tokens[idx]
    tag = token.tag  # h1, h2, h3, etc.

    # Get the inline content from the next token
    inline_token = tokens[idx + 1]
    text = inline_token.content if inline_token else ""
    slug = _slugify(text)

    # Track duplicate slugs
    slugs = env.setdefault("heading_slugs", {})
    if slug in slugs:
        slugs[slug] += 1
        slug = f"{slug}-{slugs[slug]}"
    else:
        slugs[slug] = 0

    # Store slug in env for the close renderer
    env.setdefault("heading_slug_stack", []).append(slug)

    return f'<{tag} id="{slug}" class="heading-anchor">'


def _heading_close_renderer(self, tokens, idx, options, env):
    """Render heading close tag with anchor link on the right side."""
    token = tokens[idx]
    tag = token.tag
    slug = env.get("heading_slug_stack", [None]).pop() or ""
    return f' <a href="#{slug}" class="anchor-link" aria-hidden="true">#</a></{tag}>'


def parse_readme(file_path: str) -> str:
    """Returns HTML string from README.md with raw code blocks."""
    with open(file_path, "r", encoding="utf-8") as f:
        text = f.read()

    md = MarkdownIt("commonmark", {"html": True, "typographer": True})
    md.enable("table")
    md.enable("replacements")
    md.add_render_rule("fence", _fence_renderer)
    md.add_render_rule("heading_open", _heading_open_renderer)
    md.add_render_rule("heading_close", _heading_close_renderer)

    return md.render(text)


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: readme_parser.py <path-to-readme.md>", file=sys.stderr)
        sys.exit(1)

    html = parse_readme(sys.argv[1])
    sys.stdout.buffer.write(html.encode("utf-8"))
