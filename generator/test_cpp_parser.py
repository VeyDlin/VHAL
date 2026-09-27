"""Regression tests for the C++ header parser."""

import tempfile
import unittest
from pathlib import Path

from cpp_parser import parse_header



class CppParserTests(unittest.TestCase):
    def parse_source(self, source: str) -> dict:
        with tempfile.TemporaryDirectory() as directory:
            header_path = Path(directory) / "fixture.hpp"
            header_path.write_text(source, encoding="utf-8")
            return parse_header(str(header_path))


    def test_collects_standalone_enum_inside_namespace(self) -> None:
        result = self.parse_source(
            "namespace api { enum Status { Good, Bad }; }"
        )

        self.assertEqual(
            result["symbols"],
            [
                {
                    "kind": "enum",
                    "name": "Status",
                    "values": ["Good", "Bad"],
                    "description": "",
                    "namespace": "api",
                }
            ],
        )


    def test_parses_nested_struct_fields_and_keeps_regular_members(self) -> None:
        result = self.parse_source(
            """
            class Owner {
            public:
                struct Parameters {
                    int count = 3;
                };
                int existing = 5;
                void run();
            };
            """
        )

        owner = result["symbols"][0]
        self.assertEqual(owner["name"], "Owner")
        self.assertEqual(
            owner["members"]["public"],
            [
                {
                    "kind": "struct",
                    "name": "Parameters",
                    "template": "",
                    "bases": [],
                    "description": "",
                    "members": {
                        "public": [
                            {
                                "kind": "field",
                                "name": "count",
                                "type": "int",
                                "description": "",
                                "default": "3",
                            }
                        ],
                        "protected": [],
                        "private": [],
                    },
                },
                {
                    "kind": "field",
                    "name": "existing",
                    "type": "int",
                    "description": "",
                    "default": "5",
                },
                {
                    "kind": "method",
                    "name": "run",
                    "returnType": "void",
                    "params": [],
                    "qualifiers": [],
                    "description": "",
                },
            ],
        )


    def test_parses_nested_template_class_with_template_and_base(self) -> None:
        result = self.parse_source(
            """
            class Owner {
            public:
                template<class T>
                struct Box : Base<T> {
                    T value;
                };
            };
            """
        )

        box = result["symbols"][0]["members"]["public"][0]
        self.assertEqual(box["kind"], "struct")
        self.assertEqual(box["name"], "Box")
        self.assertEqual(box["template"], "template<class T>")
        self.assertEqual(box["bases"], ["Base<T>"])
        self.assertEqual(
            box["members"]["public"],
            [
                {
                    "kind": "field",
                    "name": "value",
                    "type": "T",
                    "description": "",
                }
            ],
        )


    def test_keeps_anonymous_inline_struct_declarator_as_a_field(self) -> None:
        result = self.parse_source(
            "struct Owner { struct { int value; } data; };"
        )

        field = result["symbols"][0]["members"]["public"][0]
        self.assertEqual(field["kind"], "field")
        self.assertEqual(field["name"], "data")
        self.assertIn("struct", field["type"])


if __name__ == "__main__":
    unittest.main()
