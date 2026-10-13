#!/usr/bin/env python3
"""Embed losslessly deflated fixed legacy portal assets for ESP32 builds."""

from __future__ import annotations

import ast
import re
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src" / "legacy_portal_assets.h"
WM_OUTPUT = ROOT / "src" / "src" / "WiFiManager" / "legacy_wm_assets.h"
ASSETS = {
    "crsfCalIntro": "src/src/CRSF/crsf_wifi.h",
    "crsfCalStyle": "src/src/CRSF/crsf_wifi.h",
    "crsfExpoScript": "src/src/CRSF/crsf_wifi.h",
    "crsfCalScript": "src/src/CRSF/crsf_wifi.h",
    "crsfDisplayScript": "src/src/CRSF/crsf_wifi.h",
    "crsfLogicMixScript": "src/src/CRSF/crsf_wifi.h",
    "crsfLogicMixIntro": "src/src/CRSF/crsf_wifi.h",
    "myHead": "src/remote_wifi.cpp",
}
WM_ASSETS = {
    "myCustMenu": ("src/remote_wifi.cpp", "myCustMenu", {
        "UNI_VERSION": "@@REMOTE_VERSION@@",
        "UNI_VERSION_EXTRA": "@@REMOTE_VERSION_EXTRA@@",
        "WEBHOME": "@@REMOTE_WEBHOME@@",
    }),
    "crsfModeHelp": ("src/src/CRSF/crsf_wifi.h", "crsfModeHelpSource", {}),
    "HTTP_SCRIPT": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_SCRIPT", {}),
    "HTTP_SCRIPT_QI": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_SCRIPT_QI", {}),
    "HTTP_SCRIPT_UPL": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_SCRIPT_UPL", {}),
    "HTTP_STYLE": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE", {"HTTP_RED": "#be5c9c", "HTTP_BLUE": "#4f529d", "HTTP_BUTTON_TEXT": "#fff"}),
    "HTTP_STYLE_50S": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE", {"HTTP_RED": "#f2bde1", "HTTP_BLUE": "#d3ac72", "HTTP_BUTTON_TEXT": "#000"}),
    "HTTP_STYLE_MSG": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_MSG", {}),
    "HTTP_STYLE_QI": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_QI", {}),
    "HTTP_STYLE_SET": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_SET", {}),
    "HTTP_STYLE_STA": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_STA", {}),
    "HTTP_STYLE_UPL": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_UPL", {}),
    "HTTP_STYLE_UPLF": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_UPLF", {"HTTP_YUPD": "#ebe74c"}),
    "HTTP_STYLE_UPLF_50S": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_UPLF", {"HTTP_YUPD": "#be5c9c"}),
    "HTTP_STYLE_UPLN": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_UPLN", {}),
    "HTTP_STYLE_C80": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_C80", {"HTTP_BLUE": "#4f529d"}),
    "HTTP_STYLE_C80_NOPARAM3": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_C80", {"HTTP_BLUE": "#4f529d"}),
    "HTTP_STYLE_C80_50S": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_STYLE_C80", {}),
    "HTTP_FORM_WIFI": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_FORM_WIFI", {}),
    "HTTP_FORM_START": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_FORM_START", {}),
    "HTTP_FORM_END": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_FORM_END", {}),
    "HTTP_SCAN_LINK": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_SCAN_LINK", {}),
    "HTTP_SHOWALL_FORM": ("src/src/WiFiManager/wm_strings_en.h", "HTTP_SHOWALL_FORM", {}),
}

RAW_STRING = re.compile(r'R"([A-Za-z0-9_]*)\((.*?)\)\1"', re.DOTALL)
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def c_string_expression(expression: str, macros: dict[str, str]) -> bytes:
    """Decode the adjacent string literals and simple string macros we use."""
    values: list[str] = []
    position = 0
    while position < len(expression):
        while position < len(expression) and (expression[position].isspace() or expression[position] == "+"):
            position += 1
        if expression.startswith("//", position):
            newline = expression.find("\n", position)
            position = len(expression) if newline < 0 else newline + 1
            continue
        if position == len(expression):
            break
        raw = RAW_STRING.match(expression, position)
        if raw:
            values.append(raw.group(2))
            position = raw.end()
            continue
        literal = STRING.match(expression, position)
        if literal:
            values.append(ast.literal_eval(literal.group(0)))
            position = literal.end()
            continue
        ident = IDENT.match(expression, position)
        if ident and ident.group(0) in macros:
            values.append(macros[ident.group(0)])
            position = ident.end()
            continue
        raise ValueError(f"unsupported C string expression near {expression[position:position + 50]!r}")
    return "".join(values).encode("utf-8")


def extract_asset(path: Path, name: str, macro_overrides: dict[str, str] | None = None, defines: set[str] | None = None) -> bytes:
    source = path.read_text(encoding="utf-8")
    if name.startswith("HTTP_STYLE_C80") and defines is not None:
        source = select_preprocessor_branch(source, defines)
    macros: dict[str, str] = {}
    for macro, expression in re.findall(r"^#define\s+(\w+)\s+(.+?)\s*$", source, re.MULTILINE):
        literal = STRING.match(expression.strip())
        if literal:
            macros[macro] = ast.literal_eval(literal.group(0))
    macros.update(macro_overrides or {})
    declaration = re.search(
        rf"\b(?:static\s+)?const\s+char\s+\*?\s*{re.escape(name)}\s*(?:\[\])?\s*(?:PROGMEM\s*)?=\s*",
        source,
    )
    if not declaration:
        raise ValueError(f"cannot find asset declaration {name} in {path}")
    position = declaration.end()
    expression_end = position
    while position < len(source):
        while position < len(source) and source[position].isspace():
            position += 1
        if source[position:position + 1] == "#":
            newline = source.find("\n", position)
            position = len(source) if newline < 0 else newline + 1
            continue
        if source.startswith("//", position):
            newline = source.find("\n", position)
            position = len(source) if newline < 0 else newline + 1
            continue
        raw = RAW_STRING.match(source, position)
        literal = STRING.match(source, position)
        ident = IDENT.match(source, position)
        token = raw or literal or (ident if ident and ident.group(0) in macros else None)
        if token:
            position = token.end()
            expression_end = position
            continue
        if position < len(source) and source[position] == "+":
            position += 1
            continue
        if position < len(source) and source[position] == ";":
            for macro, value in (macro_overrides or {}).items():
                macros[macro] = value
            expression = source[declaration.end():expression_end]
            if defines is not None:
                expression = select_preprocessor_branch(expression, defines)
            return c_string_expression(expression, macros)
        raise ValueError(f"unsupported C string declaration {name} near {source[position:position + 50]!r}")
    raise ValueError(f"unterminated asset declaration {name} in {path}")


def asset_bytes() -> dict[str, bytes]:
    return {
        name: extract_asset(ROOT / path, name, defines={"HAVE_CRSF"})
        for name, path in ASSETS.items()
    }


def wm_asset_bytes() -> dict[str, bytes]:
    result = {}
    for name, (path, declaration, overrides) in WM_ASSETS.items():
        defines = None
        if name == "HTTP_STYLE_C80":
            defines = {"WM_PARAM3", "WM_PARAM2", "WM_DYNPARM3"}
        elif name.endswith("_NOPARAM3"):
            defines = {"WM_PARAM2"}
        elif name.endswith("_50S") and name.startswith("HTTP_STYLE_C80"):
            defines = {"WM_50S_STYLE"}
        elif name in ("myCustMenu", "crsfModeHelp"):
            defines = set()
        result[name] = extract_asset(ROOT / path, declaration, overrides, defines or set())
    return result


def select_preprocessor_branch(source: str, defines: set[str]) -> str:
    """Select the simple #if forms used around WiFiManager C80 strings."""
    output: list[str] = []
    active = True
    stack: list[tuple[bool, bool]] = []
    for line in source.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("#ifdef ") or stripped.startswith("#ifndef "):
            negate = stripped.startswith("#ifndef ")
            macro = stripped.split(None, 1)[1]
            test = (macro in defines) != negate
            stack.append((active, test))
            active = active and test
        elif stripped.startswith("#if "):
            expression = stripped[4:]
            terms = re.findall(r"defined\((\w+)\)", expression)
            test = all(term in defines for term in terms)
            if "!defined" in expression:
                missing = re.findall(r"!defined\((\w+)\)", expression)
                test = not any(term in defines for term in missing)
            stack.append((active, test))
            active = active and test
        elif stripped == "#else":
            parent, selected = stack[-1]
            stack[-1] = (parent, not selected)
            active = parent and not selected
        elif stripped == "#endif":
            parent, _ = stack.pop()
            active = parent
        elif active:
            output.append(line)
    return "".join(output)


def raw_deflate(data: bytes) -> bytes:
    compressor = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
    return compressor.compress(data) + compressor.flush()


def render_header(assets: dict[str, bytes], *, external: bool = False) -> str:
    lines = [
        "// Generated by tools/embed_legacy_portal.py; do not edit.",
        "#pragma once",
        "#if defined(ARDUINO_ARCH_ESP32)",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "#include <esp32/rom/miniz.h>",
        "",
        "#ifndef LEGACY_PORTAL_HELPERS_INCLUDED",
        "#define LEGACY_PORTAL_HELPERS_INCLUDED 1",
        "",
        "struct LegacyPortalFragment {",
        "    size_t offset;",
        "    const uint8_t *compressed;",
        "    size_t compressedSize;",
        "    size_t expandedSize;",
        "};",
        "",
        "inline bool legacyPortalInflate(const uint8_t *compressed, size_t compressedSize, char *output, size_t expandedSize)",
        "{",
        "    tinfl_decompressor *state = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));",
        "    if(!state) return false;",
        "    tinfl_init(state);",
        "    size_t inputSize = compressedSize;",
        "    size_t outputSize = expandedSize;",
        "    tinfl_status status = tinfl_decompress(state, compressed, &inputSize, (mz_uint8*)output, (mz_uint8*)output, &outputSize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);",
        "    free(state);",
        "    return status == TINFL_STATUS_DONE && inputSize == compressedSize && outputSize == expandedSize;",
        "}",
        "",
        "inline char *legacyPortalBuildOutput(const char *source, size_t sourceSize, const LegacyPortalFragment *fragments, size_t fragmentCount)",
        "{",
        "    size_t expandedSize = sourceSize;",
        "    for(size_t i = 0; i < fragmentCount; i++) expandedSize += fragments[i].expandedSize;",
        "    char *output = (char *)malloc(expandedSize + 1);",
        "    if(!output) return nullptr;",
        "    size_t sourceOffset = 0, outputOffset = 0;",
        "    for(size_t i = 0; i < fragmentCount; i++) {",
        "        if(fragments[i].offset < sourceOffset || fragments[i].offset > sourceSize) { free(output); return nullptr; }",
        "        size_t copySize = fragments[i].offset - sourceOffset;",
        "        memcpy(output + outputOffset, source + sourceOffset, copySize);",
        "        outputOffset += copySize; sourceOffset += copySize;",
        "        if(!legacyPortalInflate(fragments[i].compressed, fragments[i].compressedSize, output + outputOffset, fragments[i].expandedSize)) { free(output); return nullptr; }",
        "        outputOffset += fragments[i].expandedSize;",
        "    }",
        "    memcpy(output + outputOffset, source + sourceOffset, sourceSize - sourceOffset);",
        "    output[expandedSize] = 0;",
        "    return output;",
        "}",
        "",
        "inline bool legacyPortalAppend(char *output, const uint8_t *compressed, size_t compressedSize, size_t expandedSize)",
        "{",
        "    const size_t offset = strlen(output);",
        "    if(!legacyPortalInflate(compressed, compressedSize, output + offset, expandedSize)) return false;",
        "    output[offset + expandedSize] = 0;",
        "    return true;",
        "}",
        "",
        "inline bool legacyPortalAppendString(String &output, const uint8_t *compressed, size_t compressedSize, size_t expandedSize)",
        "{",
        "    const size_t offset = output.length();",
        "    if(!output.reserve(offset + expandedSize)) return false;",
        "    char filler[64]; memset(filler, ' ', sizeof(filler));",
        "    for(size_t left = expandedSize; left;) {",
        "        size_t count = left < sizeof(filler) ? left : sizeof(filler);",
        "        if(!output.concat(filler, count)) { output.remove(offset); return false; }",
        "        left -= count;",
        "    }",
        "    if(!legacyPortalInflate(compressed, compressedSize, output.begin() + offset, expandedSize)) { output.remove(offset); return false; }",
        "    return true;",
        "}",
        "",
        "inline bool legacyPortalInsertString(String &output, size_t offset, const uint8_t *compressed, size_t compressedSize, size_t expandedSize)",
        "{",
        "    const size_t oldSize = output.length();",
        "    if(offset > oldSize || !output.reserve(oldSize + expandedSize)) return false;",
        "    char filler[64]; memset(filler, ' ', sizeof(filler));",
        "    for(size_t left = expandedSize; left;) {",
        "        size_t count = left < sizeof(filler) ? left : sizeof(filler);",
        "        if(!output.concat(filler, count)) { output.remove(oldSize); return false; }",
        "        left -= count;",
        "    }",
        "    char *data = output.begin();",
        "    const size_t tailSize = oldSize - offset;",
        "    memmove(data + offset + expandedSize, data + offset, tailSize);",
        "    if(!legacyPortalInflate(compressed, compressedSize, data + offset, expandedSize)) {",
        "        memmove(data + offset, data + offset + expandedSize, tailSize);",
        "        output.remove(oldSize);",
        "        return false;",
        "    }",
        "    return true;",
        "}",
        "",
        "#endif // LEGACY_PORTAL_HELPERS_INCLUDED",
        "",
    ]
    names = set(assets)
    for name, raw in assets.items():
        base_name = name[:-4] if name.endswith("_50S") else name
        if name.startswith("crsf"):
            lines.append("#ifdef HAVE_CRSF")
        if name == "HTTP_STYLE_C80":
            lines.append("#if !defined(WM_50S_STYLE) && defined(WM_PARAM3)")
        elif name == "HTTP_STYLE_C80_NOPARAM3":
            lines.append("#if !defined(WM_50S_STYLE) && !defined(WM_PARAM3)")
        elif name == "HTTP_STYLE_C80_50S":
            lines.append("#ifdef WM_50S_STYLE")
        elif name.endswith("_50S"):
            lines.append("#ifdef WM_50S_STYLE")
        elif f"{base_name}_50S" in names:
            lines.append("#ifndef WM_50S_STYLE")
        if external:
            lines.append("#ifdef LEGACY_WM_ASSET_IMPLEMENTATION")
        compressed = raw_deflate(raw)
        storage = "extern const" if external else "static const"
        lines.append(f"{storage} uint8_t legacy_{name}_deflate[] PROGMEM = {{")
        for offset in range(0, len(compressed), 12):
            chunk = compressed[offset:offset + 12]
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
        lines.append("};")
        size_storage = "extern const" if external else "static constexpr"
        lines.append(f"{size_storage} size_t legacy_{name}_compressed_size = sizeof(legacy_{name}_deflate);")
        lines.append(f"{size_storage} size_t legacy_{name}_expanded_size = {len(raw)};")
        lines.append("")
        if external:
            lines.append("#else // LEGACY_WM_ASSET_IMPLEMENTATION")
            lines.append(f"extern const uint8_t legacy_{name}_deflate[] PROGMEM;")
            lines.append(f"extern const size_t legacy_{name}_compressed_size;")
            lines.append(f"extern const size_t legacy_{name}_expanded_size;")
            lines.append("#endif")
            lines.append("")
        if name.startswith("HTTP_STYLE_C80") or name.endswith("_50S") or f"{base_name}_50S" in names:
            lines.append("#endif // WM_50S_STYLE variant")
            lines.append("")
        if name.startswith("crsf"):
            lines.append("#endif // HAVE_CRSF")
            lines.append("")
    lines.append("#endif // ARDUINO_ARCH_ESP32")
    return "\n".join(lines) + "\n"


def main() -> None:
    assets = asset_bytes()
    wm_assets = wm_asset_bytes()
    OUTPUT.write_text(render_header(assets), encoding="utf-8", newline="\n")
    WM_OUTPUT.write_text(render_header(wm_assets, external=True), encoding="utf-8", newline="\n")
    all_assets = {
        **assets,
        **{
            name: value for name, value in wm_assets.items()
            if not name.endswith("_50S") and not name.endswith("_NOPARAM3")
        },
    }
    raw_total = sum(map(len, all_assets.values()))
    compressed_total = sum(map(len, (raw_deflate(value) for value in all_assets.values())))
    print(f"embedded assets: raw={raw_total} deflate={compressed_total} savings={raw_total - compressed_total} bytes")


if __name__ == "__main__":
    main()
