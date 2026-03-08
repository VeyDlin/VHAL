"""
C++ header parser using tree-sitter-cpp.

Extracts classes, structs, enums, methods, fields, and comments
from C++ header files into a structured dictionary format.
"""

import os
import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser

CPP = Language(tscpp.language())
parser = Parser(CPP)


def _node_text(node):
    """Get the text content of a tree-sitter node."""
    if node is None:
        return ""
    return node.text.decode("utf-8").replace("\r\n", "\n").replace("\r", "")


def _get_comment_before(node):
    """
    Collect comment text from sibling nodes immediately before this node.
    Returns the cleaned-up comment string.
    """
    comments = []
    sibling = node.prev_named_sibling
    while sibling is not None and sibling.type == "comment":
        comments.insert(0, sibling)
        sibling = sibling.prev_named_sibling

    if not comments:
        return ""

    lines = []
    for c in comments:
        text = _node_text(c).strip()
        # Strip // prefix, preserve indentation after it
        if text.startswith("//"):
            text = text[2:]
            # Remove at most one leading space (conventional "// comment" style)
            if text.startswith(" "):
                text = text[1:]
        # Strip /* */ delimiters
        elif text.startswith("/*") and text.endswith("*/"):
            text = text[2:-2].strip()
        # Skip decorative lines like "// ===================="
        if text and not all(ch in "=-~ " for ch in text):
            lines.append(text)

    return "\n".join(lines)


def _extract_qualifiers(func_node):
    """Extract qualifiers (static, const, virtual, override, constexpr) from a function definition."""
    qualifiers = []
    for child in func_node.children:
        if child.type == "storage_class_specifier":
            val = _node_text(child)
            if val == "static":
                qualifiers.append("static")
        elif child.type == "type_qualifier":
            val = _node_text(child)
            if val == "constexpr":
                qualifiers.append("constexpr")
        elif child.type == "virtual":
            qualifiers.append("virtual")

    # Check function_declarator for const qualifier and override
    for child in func_node.children:
        if child.type == "function_declarator":
            for dc in child.children:
                if dc.type == "type_qualifier" and _node_text(dc) == "const":
                    qualifiers.append("const")
                if dc.type == "virtual_specifier" and _node_text(dc) == "override":
                    qualifiers.append("override")

    return qualifiers


def _extract_return_type(func_node):
    """Extract the return type from a function definition or declaration."""
    # The return type can be: primitive_type, type_identifier, qualified_identifier,
    # template_type, etc. It's typically the children before the function_declarator,
    # excluding storage_class_specifier, type_qualifier (constexpr/virtual), etc.
    parts = []
    for child in func_node.children:
        if child.type == "function_declarator":
            break
        if child.type == "compound_statement":
            break
        if child.type in ("storage_class_specifier", "virtual"):
            continue
        if child.type == "type_qualifier":
            val = _node_text(child)
            if val == "constexpr":
                continue
            # Other type qualifiers like 'const' are part of return type
            parts.append(val)
        elif child.type in (
            "primitive_type", "type_identifier", "qualified_identifier",
            "template_type", "sized_type_specifier", "placeholder_type_specifier",
            "decltype",
        ):
            parts.append(_node_text(child))
        elif child.type == "&" or child.type == "*":
            parts.append(_node_text(child))

    return " ".join(parts) if parts else "void"


def _extract_params(func_declarator):
    """Extract parameter list from a function declarator."""
    params = []
    param_list = None
    for child in func_declarator.children:
        if child.type == "parameter_list":
            param_list = child
            break

    if param_list is None:
        return params

    for child in param_list.children:
        if child.type in ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration"):
            param = _parse_parameter(child)
            if param:
                params.append(param)

    return params


def _parse_parameter(param_node):
    """Parse a single parameter declaration node into {type, name}."""
    children = list(param_node.children)

    name = ""
    type_parts = []

    # Find the name node - could be identifier, or wrapped in reference/pointer/variadic_declarator
    is_variadic = param_node.type == "variadic_parameter_declaration"
    name_node = None
    for child in reversed(children):
        if child.type in ("identifier", "field_identifier"):
            name_node = child
            break
        elif child.type == "variadic_declarator":
            for vc in child.children:
                if vc.type in ("identifier", "field_identifier"):
                    name_node = vc
                    break
            if name_node:
                break
        elif child.type == "reference_declarator":
            # reference_declarator contains & and identifier
            for rc in child.children:
                if rc.type in ("identifier", "field_identifier"):
                    name_node = rc
                    break
            if name_node:
                break
            name_node = child
            break
        elif child.type == "pointer_declarator":
            for pc in child.children:
                if pc.type in ("identifier", "field_identifier"):
                    name_node = pc
                    break
            if name_node:
                break
            name_node = child
            break
        elif child.type == "array_declarator":
            name_node = child
            break

    if name_node:
        name = _node_text(name_node).strip()

    # Collect type parts: everything before the declarator that holds the name,
    # but also include & or * from reference/pointer declarators
    found_ref = False
    found_ptr = False
    for child in children:
        if child is name_node:
            break
        if child.type == "variadic_declarator":
            break
        if child.type == "reference_declarator":
            found_ref = True
            break
        if child.type == "pointer_declarator":
            found_ptr = True
            break
        if child.type in ("=", "default_value"):
            break
        if child.type in ("(", ")", ",", "..."):
            continue
        type_parts.append(_node_text(child))

    type_str = " ".join(type_parts)
    if found_ref:
        type_str += "&"
    if found_ptr:
        type_str += "*"
    if is_variadic:
        type_str += "..."

    return {"type": type_str, "name": name}


def _extract_template_str(template_node):
    """Extract the full template<...> string from a template_declaration node."""
    for child in template_node.children:
        if child.type == "template_parameter_list":
            return "template" + _node_text(child)
    return ""


def _extract_bases(class_node):
    """Extract base class names from a class/struct specifier."""
    bases = []
    for child in class_node.children:
        if child.type == "base_class_clause":
            for bc in child.children:
                if bc.type == "type_identifier" or bc.type == "qualified_identifier" or bc.type == "template_type":
                    bases.append(_node_text(bc))
    return bases


def _extract_enum(enum_spec):
    """Extract enum name and values from an enum_specifier node."""
    name = ""
    base_type = ""
    values = []
    is_enum_class = False
    found_name = False

    for child in enum_spec.children:
        if child.type == "class" or child.type == "struct":
            is_enum_class = True
        elif child.type == "type_identifier":
            if not found_name:
                # First type_identifier is the enum name
                name = _node_text(child)
                found_name = True
            else:
                # Second type_identifier is the base type (e.g. uint8)
                base_type = _node_text(child)
        elif child.type == "enumerator_list":
            for ec in child.children:
                if ec.type == "enumerator":
                    for ecc in ec.children:
                        if ecc.type == "identifier":
                            values.append(_node_text(ecc))
                            break

    return name, values, is_enum_class


def _parse_field_declaration(node, description):
    """Parse a field_declaration node. Could be an enum, a regular field, or a function declaration."""
    # Check if it contains an enum_specifier
    for child in node.children:
        if child.type == "enum_specifier":
            name, values, is_enum_class = _extract_enum(child)
            return {
                "kind": "enum",
                "name": name,
                "values": values,
                "description": description,
            }

    # Regular field declaration
    # Gather type parts and field name
    type_parts = []
    field_name = ""
    default_value = ""
    hit_equals = False
    found_name = False

    for child in node.children:
        if child.type == ";":
            continue
        if child.type == "field_identifier":
            field_name = _node_text(child)
            found_name = True
        elif child.type == "=":
            hit_equals = True
        elif hit_equals and child.type != ";":
            default_value = _node_text(child)
        elif child.type == "initializer_list":
            # Brace initializer like T value{} or T value{42}
            init_text = _node_text(child).strip()
            if init_text and init_text != "{}":
                default_value = init_text
        elif child.type in ("function_declarator",):
            # This is a function declaration, not a field
            return _parse_func_declaration(node, description)
        elif not found_name and not hit_equals:
            type_parts.append(_node_text(child))

    type_str = " ".join(type_parts)

    result = {
        "kind": "field",
        "name": field_name,
        "type": type_str,
        "description": description,
    }
    if default_value:
        result["default"] = default_value

    return result


def _parse_func_declaration(node, description):
    """Parse a function declaration (not definition) inside a field_declaration."""
    qualifiers = _extract_qualifiers(node)
    return_type = _extract_return_type(node)

    func_name = ""
    params = []

    for child in node.children:
        if child.type == "function_declarator":
            for dc in child.children:
                if dc.type in ("field_identifier", "identifier"):
                    func_name = _node_text(dc)
                elif dc.type == "operator_name":
                    func_name = _node_text(dc)
                elif dc.type == "parameter_list":
                    params = _extract_params(child)
                    break

    return {
        "kind": "method",
        "name": func_name,
        "returnType": return_type,
        "params": params,
        "qualifiers": qualifiers,
        "description": description,
    }


def _find_function_declarator(node):
    """Find the function_declarator in a function_definition, even if nested in reference/pointer declarator."""
    for child in node.children:
        if child.type == "function_declarator":
            return child
        # function_declarator can be nested inside reference_declarator or pointer_declarator
        # e.g. Console& operator<<(...) → reference_declarator { & function_declarator { ... } }
        if child.type in ("reference_declarator", "pointer_declarator"):
            for nested in child.children:
                if nested.type == "function_declarator":
                    return nested
    return None


def _parse_function_definition(node, description):
    """Parse a function_definition node into a method dict."""
    qualifiers = _extract_qualifiers(node)
    return_type = _extract_return_type(node)

    func_name = ""
    params = []

    func_decl = _find_function_declarator(node)
    if func_decl:
        for dc in func_decl.children:
            if dc.type in ("field_identifier", "identifier"):
                func_name = _node_text(dc)
            elif dc.type == "destructor_name":
                func_name = _node_text(dc)
            elif dc.type == "operator_name":
                func_name = _node_text(dc)
        params = _extract_params(func_decl)

        # Check for reference return type (& before function_declarator)
        for child in node.children:
            if child.type == "reference_declarator":
                return_type += "&"
                break
            if child.type == "pointer_declarator":
                return_type += "*"
                break
    else:
        for child in node.children:
            if child.type == "operator_cast":
                # operator bool(), operator int(), etc.
                cast_type = ""
                for dc in child.children:
                    if dc.type in ("primitive_type", "type_identifier", "qualified_identifier", "template_type"):
                        cast_type = _node_text(dc)
                    elif dc.type == "abstract_function_declarator":
                        for adc in dc.children:
                            if adc.type == "type_qualifier" and _node_text(adc) == "const":
                                if "const" not in qualifiers:
                                    qualifiers.append("const")
                func_name = f"operator {cast_type}"
                return_type = ""
                break

    # Constructor detection: no type nodes before function_declarator means constructor
    has_type_before_decl = False
    for child in node.children:
        if child.type in ("function_declarator", "operator_cast"):
            break
        if child.type in (
            "primitive_type", "type_identifier", "qualified_identifier",
            "template_type", "sized_type_specifier", "placeholder_type_specifier",
            "decltype",
        ):
            has_type_before_decl = True
            break
    if not has_type_before_decl and func_name:
        return_type = ""

    return {
        "kind": "method",
        "name": func_name,
        "returnType": return_type,
        "params": params,
        "qualifiers": qualifiers,
        "description": description,
    }


def _parse_class_body(field_list_node, default_access="private"):
    """
    Parse the field_declaration_list of a class/struct.
    Returns members dict with public/protected/private lists.
    """
    members = {"public": [], "protected": [], "private": []}
    current_access = default_access

    children = list(field_list_node.children)
    i = 0
    while i < len(children):
        child = children[i]

        if child.type == "access_specifier":
            current_access = _node_text(child).strip()
            i += 1
            continue

        if child.type in ("{", "}", ":", ";"):
            i += 1
            continue

        if child.type == "comment":
            i += 1
            continue

        description = _get_comment_before(child)

        if child.type == "field_declaration":
            member = _parse_field_declaration(child, description)
            if member:
                members[current_access].append(member)

        elif child.type == "function_definition":
            member = _parse_function_definition(child, description)
            if member:
                members[current_access].append(member)

        elif child.type == "declaration":
            # Could be a function declaration
            member = _parse_declaration(child, description)
            if member:
                members[current_access].append(member)

        elif child.type == "template_declaration":
            # Template method or nested template class inside the class
            member = _parse_template_member(child, description)
            if member:
                members[current_access].append(member)

        elif child.type == "friend_declaration":
            pass  # skip friends

        elif child.type == "using_declaration" or child.type == "alias_declaration" or child.type == "type_definition":
            pass  # skip using/typedef

        i += 1

    return members


def _parse_declaration(node, description):
    """Parse a declaration node (e.g. function declaration without body)."""
    # Check if it has a function_declarator
    has_func_decl = False
    for child in node.children:
        if child.type == "function_declarator":
            has_func_decl = True
            break
        if child.type == "init_declarator":
            for ic in child.children:
                if ic.type == "function_declarator":
                    has_func_decl = True
                    break

    if has_func_decl:
        qualifiers = _extract_qualifiers(node)
        return_type = _extract_return_type(node)
        func_name = ""
        params = []

        for child in node.children:
            if child.type == "function_declarator":
                for dc in child.children:
                    if dc.type in ("identifier", "field_identifier"):
                        func_name = _node_text(dc)
                params = _extract_params(child)
                break

        return {
            "kind": "method",
            "name": func_name,
            "returnType": return_type,
            "params": params,
            "qualifiers": qualifiers,
            "description": description,
        }

    return None


def _parse_template_member(template_node, description):
    """Parse a template_declaration that is a member of a class (template method or nested class)."""
    template_str = _extract_template_str(template_node)
    inner = None

    for child in template_node.children:
        if child.type in ("function_definition", "declaration", "field_declaration"):
            inner = child
            break
        if child.type in ("class_specifier", "struct_specifier"):
            inner = child
            break

    if inner is None:
        return None

    if inner.type == "function_definition":
        member = _parse_function_definition(inner, description)
        member["template"] = template_str
        return member
    elif inner.type == "declaration":
        member = _parse_declaration(inner, description)
        if member:
            member["template"] = template_str
        return member

    return None


def _is_forward_declaration(node):
    """Check if a class/struct specifier is a forward declaration (no body)."""
    for child in node.children:
        if child.type == "field_declaration_list":
            return False
    return True


def _parse_class_or_struct(node, description, template_str=""):
    """
    Parse a class_specifier or struct_specifier into a symbol dict.
    Returns None for forward declarations (no body).
    """
    if _is_forward_declaration(node):
        return None

    kind = "class" if node.type == "class_specifier" else "struct"
    default_access = "private" if kind == "class" else "public"
    name = ""
    bases = _extract_bases(node)

    for child in node.children:
        if child.type == "type_identifier":
            name = _node_text(child)
            break

    members = {"public": [], "protected": [], "private": []}
    for child in node.children:
        if child.type == "field_declaration_list":
            members = _parse_class_body(child, default_access)
            break

    result = {
        "kind": kind,
        "name": name,
        "template": template_str,
        "bases": bases,
        "description": description,
        "members": members,
    }

    return result


def _parse_top_level_enum(node, description):
    """Parse a top-level enum declaration."""
    # node might be a declaration containing an enum_specifier
    enum_spec = None
    if node.type == "enum_specifier":
        enum_spec = node
    else:
        for child in node.children:
            if child.type == "enum_specifier":
                enum_spec = child
                break

    if enum_spec is None:
        return None

    name, values, is_enum_class = _extract_enum(enum_spec)
    return {
        "kind": "enum",
        "name": name,
        "values": values,
        "description": description,
    }


def _collect_symbols(children, namespace=""):
    """Collect API symbols from a list of tree-sitter nodes."""
    symbols = []

    for child in children:
        description = _get_comment_before(child)

        if child.type == "namespace_definition":
            # Get namespace name
            ns_name = ""
            for nc in child.children:
                if nc.type == "identifier" or nc.type == "namespace_identifier":
                    ns_name = _node_text(nc)
                    break
            full_ns = f"{namespace}::{ns_name}" if namespace else ns_name
            # Recurse into namespace body
            for nc in child.children:
                if nc.type == "declaration_list":
                    symbols.extend(_collect_symbols(nc.children, full_ns))

        elif child.type == "template_declaration":
            template_str = _extract_template_str(child)
            inner = None
            for tc in child.children:
                if tc.type in ("class_specifier", "struct_specifier"):
                    inner = tc
                    break
                if tc.type == "declaration":
                    inner = tc
                    break
                if tc.type == "function_definition":
                    inner = tc
                    break

            if inner is not None:
                if inner.type in ("class_specifier", "struct_specifier"):
                    sym = _parse_class_or_struct(inner, description, template_str)
                    if sym:
                        _add_sym(symbols, sym, namespace)
                elif inner.type == "function_definition":
                    sym = _parse_function_definition(inner, description)
                    sym["template"] = template_str
                    _add_sym(symbols, sym, namespace)
                elif inner.type == "declaration":
                    sym = _parse_declaration(inner, description)
                    if sym:
                        sym["template"] = template_str
                        _add_sym(symbols, sym, namespace)

        elif child.type in ("class_specifier", "struct_specifier"):
            sym = _parse_class_or_struct(child, description)
            if sym:
                _add_sym(symbols, sym, namespace)

        elif child.type == "declaration":
            for dc in child.children:
                if dc.type == "enum_specifier":
                    sym = _parse_top_level_enum(child, description)
                    if sym:
                        _add_sym(symbols, sym, namespace)
                    break

        elif child.type == "function_definition":
            sym = _parse_function_definition(child, description)
            _add_sym(symbols, sym, namespace)

    return symbols


def _add_sym(symbols, sym, namespace):
    """Append symbol to list, adding namespace if present."""
    if namespace:
        sym["namespace"] = namespace
    symbols.append(sym)


def parse_header(file_path: str) -> dict:
    """Parse a C++ header file and extract API symbols."""
    with open(file_path, "rb") as f:
        source = f.read()

    tree = parser.parse(source)
    root = tree.root_node

    symbols = _collect_symbols(root.children)

    return {
        "file": os.path.basename(file_path),
        "symbols": symbols,
    }


if __name__ == "__main__":
    import sys
    import json

    if len(sys.argv) < 2:
        print("Usage: cpp_parser.py <header_file>")
        sys.exit(1)

    result = parse_header(sys.argv[1])
    print(json.dumps(result, indent=2))
