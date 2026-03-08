"""
Directory scanner — builds a navigation tree from VHAL source directories.

Recursively scans directories starting from root mappings and produces a tree.
Each directory becomes a folder node; each .h file becomes a leaf child.

Documentation lives in a separate .wiki/ directory that mirrors the source tree:
  - .wiki/{rel_path}/README.md  → documentation for the folder
  - .wiki/{rel_path}/Foo.md    → documentation for Foo.h (matched by name)

Tree structure: folders first (alphabetical), then files (alphabetical).
Directories with no content anywhere in their subtree are pruned.
"""

import os
from pathlib import Path


# Directories to skip (third-party libraries, vendor CMSIS/HAL/startup)
EXCLUDE_DIRS = {
    "Drivers/CMSIS",
    "Drivers/HAL",
    "FreeRTOS/Kernel",
}

# Directories under Port/ that contain vendor device/startup files
PORT_EXCLUDE_DIRS = {
    "Drivers",
    "Startup",
}


def _is_excluded(rel_path: str) -> bool:
    """Check if a path falls under an excluded third-party directory."""
    normalized = rel_path.replace("\\", "/")
    for excl in EXCLUDE_DIRS:
        idx = normalized.find(excl)
        if idx != -1:
            if idx == 0 or normalized[idx - 1] == "/":
                return True
    # Exclude vendor directories under Port/
    if "/Port/" in normalized or normalized.startswith("Port/"):
        dir_name = normalized.rsplit("/", 1)[-1] if "/" in normalized else normalized
        if dir_name in PORT_EXCLUDE_DIRS:
            return True
    return False


ROOT_MAPPINGS = [
    {"label": "VHAL", "path": "VHAL", "docs_only": True},
    {"label": "Drivers", "path": "Common/Drivers"},
    {"label": "Utilities", "path": "Common/Utilities"},
    {
        "label": "Periphery",
        "path": None,
        "children_mappings": [
            {"label": "Adapter", "path": "Periphery/Adapter"},
            {"label": "OS", "path": "Periphery/OS"},
            {"label": "System", "path": "Periphery/System"},
        ],
    },
]


def scan_directory(base_path: str, docs_root: str, rel_path: str) -> dict | None:
    """
    Scan a directory and return a tree node, or None if no content in subtree.

    Returns folder nodes with .h files as leaf children.
    """
    abs_path = os.path.join(base_path, rel_path)

    if not os.path.isdir(abs_path):
        return None

    if _is_excluded(rel_path):
        return None

    label = os.path.basename(rel_path)
    docs_dir = os.path.join(docs_root, rel_path)

    # Check for README.md in docs mirror
    has_readme = os.path.isfile(os.path.join(docs_dir, "README.md"))

    # Collect .h files as leaf children
    file_children = []
    try:
        entries = sorted(os.listdir(abs_path))
    except PermissionError:
        entries = []

    for fname in entries:
        if fname.lower().endswith(".h") and os.path.isfile(os.path.join(abs_path, fname)):
            file_base = os.path.splitext(fname)[0]
            md_name = file_base + ".md"
            has_file_docs = os.path.isfile(os.path.join(docs_dir, md_name)) if os.path.isdir(docs_dir) else False

            file_children.append({
                "label": fname,
                "path": rel_path + "/" + fname,
                "type": "file",
                "hasReadme": has_file_docs,
                "hasHeaders": True,
                "headerDocs": [md_name] if has_file_docs else [],
                "children": [],
            })

    # Recurse into subdirectories
    dir_children = []
    for entry in entries:
        entry_path = os.path.join(abs_path, entry)
        if os.path.isdir(entry_path):
            child_rel = rel_path + "/" + entry
            child_node = scan_directory(base_path, docs_root, child_rel)
            if child_node is not None:
                dir_children.append(child_node)

    # Folders first, then files
    children = dir_children + file_children

    # Pruning: no README, no files, no children with content → skip
    if not has_readme and not file_children and not dir_children:
        return None

    return {
        "label": label,
        "path": rel_path,
        "type": "directory",
        "hasReadme": has_readme,
        "hasHeaders": False,
        "headerDocs": [],
        "children": children,
    }


def scan_docs_only(docs_root: str, rel_path: str) -> dict | None:
    """
    Scan a docs-only directory (no source .h files).
    Each .md file becomes a page, subdirectories are scanned recursively.
    """
    abs_path = os.path.join(docs_root, rel_path)
    if not os.path.isdir(abs_path):
        return None

    label = os.path.basename(rel_path)
    has_readme = os.path.isfile(os.path.join(abs_path, "README.md"))

    try:
        entries = sorted(os.listdir(abs_path))
    except PermissionError:
        entries = []

    # .md files (except README) become page children
    file_children = []
    for fname in entries:
        if fname.lower().endswith(".md") and fname != "README.md":
            fpath = os.path.join(abs_path, fname)
            if os.path.isfile(fpath):
                page_name = os.path.splitext(fname)[0]
                # Parse optional "N. " ordering prefix
                order = float("inf")
                display_name = page_name
                path_name = page_name.replace(" ", "_")
                if ". " in page_name:
                    prefix, rest = page_name.split(". ", 1)
                    if prefix.isdigit():
                        order = int(prefix)
                        display_name = rest
                        path_name = rest.replace(" ", "_")
                file_children.append({
                    "label": display_name,
                    "path": rel_path + "/" + path_name,
                    "type": "file",
                    "hasReadme": True,
                    "hasHeaders": False,
                    "headerDocs": [],
                    "docsFile": fname,
                    "children": [],
                    "_order": order,
                })

    # Recurse into subdirectories
    dir_children = []
    for entry in entries:
        entry_path = os.path.join(abs_path, entry)
        if os.path.isdir(entry_path):
            child_node = scan_docs_only(docs_root, rel_path + "/" + entry)
            if child_node is not None:
                dir_children.append(child_node)

    # Sort files by order prefix, then strip internal _order key
    file_children.sort(key=lambda x: x["_order"])
    for fc in file_children:
        del fc["_order"]

    children = dir_children + file_children

    if not has_readme and not file_children and not dir_children:
        return None

    return {
        "label": label,
        "path": rel_path,
        "type": "directory",
        "hasReadme": has_readme,
        "hasHeaders": False,
        "headerDocs": [],
        "children": children,
    }


def build_navigation(vhal_root: str, docs_root: str) -> list[dict]:
    """Build the full navigation tree from ROOT_MAPPINGS."""
    result = []

    for mapping in ROOT_MAPPINGS:
        if "children_mappings" in mapping:
            children = []
            for child_mapping in mapping["children_mappings"]:
                node = scan_directory(vhal_root, docs_root, child_mapping["path"])
                if node is not None:
                    node["label"] = child_mapping["label"]
                    children.append(node)

            if children:
                result.append({
                    "label": mapping["label"],
                    "path": mapping["path"],
                    "type": "directory",
                    "hasReadme": False,
                    "hasHeaders": False,
                    "headerDocs": [],
                    "children": children,
                })
        elif mapping.get("docs_only"):
            node = scan_docs_only(docs_root, mapping["path"])
            if node is not None:
                node["label"] = mapping["label"]
                result.append(node)
        else:
            node = scan_directory(vhal_root, docs_root, mapping["path"])
            if node is not None:
                node["label"] = mapping["label"]
                result.append(node)

    return result


if __name__ == "__main__":
    import argparse
    import json

    parser = argparse.ArgumentParser(description="Scan VHAL directories and build navigation tree")
    parser.add_argument("--vhal-root", required=True, help="Absolute path to VHAL root")
    parser.add_argument("--docs-root", required=True, help="Absolute path to docs directory")
    args = parser.parse_args()

    nav = build_navigation(args.vhal_root, args.docs_root)
    print(json.dumps(nav, indent=2))
