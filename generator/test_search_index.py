import unittest

from generate import _build_search_index as build_search_index
from generate import _strip_html as strip_html


class SearchIndexTests(unittest.TestCase):
    def test_preserves_adjacent_inline_text(self) -> None:
        self.assertEqual(strip_html('<strong>Write</strong><em>Array</em>'), 'WriteArray')


    def test_decodes_character_references(self) -> None:
        html: str = '<p>Read&lt;uint8&gt; &amp; &quot;value&quot; &#39;it&#39;</p>'

        self.assertEqual(strip_html(html), 'Read<uint8> & "value" \'it\'')


    def test_adds_space_for_line_breaks(self) -> None:
        self.assertEqual(strip_html('first<br>second'), 'first second')


    def test_excludes_hidden_and_control_content(self) -> None:
        html: str = (
            'A<span hidden>hidden <b>nested</b></span>B'
            '<script>script text</script><style>style text</style>'
            '<noscript>fallback text</noscript><button>button text</button>'
            '<div aria-hidden="true"><p>hidden paragraph</p></div>'
            '<a class="external anchor-link selected">#</a><img hidden>'
            '<p>Visible</p>'
        )

        self.assertEqual(strip_html(html), 'AB Visible')


    def test_search_text_matches_visible_entities_and_excludes_heading_links(self) -> None:
        pages: list[dict] = [{
            "path": "Example.h",
            "label": "Example",
            "readme": '<h1>Example <a class="anchor-link" href="#example">#</a></h1>'
                      '<p>Call <code>Read&lt;uint8&gt;</code> &amp; check.</p>',
            "api": [],
        }]

        index: list[dict] = build_search_index(pages)

        self.assertEqual(index[0]["readme"], "Example Call Read<uint8> & check.")


    def test_records_structural_origins_for_nested_protected_api_tokens(self) -> None:
        pages: list[dict] = [{
            'path': 'Example.h', 'label': 'Example', 'readme': '',
            'api': [{'file': 'Example.h', 'symbols': [{
                'kind': 'class', 'name': 'Device', 'members': {
                    'public': [{'kind': 'method', 'name': 'Start', 'returnType': 'void', 'qualifiers': ['constexpr']}],
                    'protected': [{'kind': 'struct', 'name': 'Inner', 'members': {
                        'private': [{'kind': 'field', 'name': 'state', 'type': 'uint32'}],
                    }}],
                },
            }]}],
        }]

        document: dict = build_search_index(pages)[0]

        self.assertEqual(document['symbols'], 'Device Start void constexpr Inner state uint32')
        self.assertEqual(document['symbolOrigins'], [
            {'start': 0, 'key': 'f0/s0'},
            {'start': 1, 'key': 'f0/s0/public/0'},
            {'start': 4, 'key': 'f0/s0/protected/0'},
            {'start': 5, 'key': 'f0/s0/protected/0/private/0'},
        ])
