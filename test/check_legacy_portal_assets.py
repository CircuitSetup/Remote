"""Check that generated legacy portal assets expand byte-for-byte to their source."""

from __future__ import annotations

import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import embed_legacy_portal as embed  # noqa: E402


def header_assets(path: Path) -> dict[str, tuple[bytes, int]]:
    source = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"(?:static const|extern const) uint8_t legacy_(\w+)_deflate\[\] PROGMEM = \{(.*?)\};\s*"
        r"(?:static constexpr|extern const) size_t legacy_\1_compressed_size = sizeof\(legacy_\1_deflate\);\s*"
        r"(?:static constexpr|extern const) size_t legacy_\1_expanded_size = (\d+);",
        re.DOTALL,
    )
    result: dict[str, tuple[bytes, int]] = {}
    for match in pattern.finditer(source):
        encoded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(2)))
        result[match.group(1)] = (encoded, int(match.group(3)))
    return result


def verify_group(generated: dict[str, tuple[bytes, int]], sources: dict[str, bytes]) -> tuple[int, int]:
    assert set(generated) == set(sources), (sorted(generated), sorted(sources))
    raw_total = compressed_total = 0
    for name, source_bytes in sources.items():
        encoded, expanded_size = generated[name]
        expanded = zlib.decompress(encoded, wbits=-zlib.MAX_WBITS)
        assert expanded_size == len(source_bytes), name
        assert expanded == source_bytes, name
        raw_total += len(source_bytes)
        compressed_total += len(encoded)
    return raw_total, compressed_total


def main() -> None:
    portal_sources = embed.asset_bytes()
    wm_sources = embed.wm_asset_bytes()
    portal_header = ROOT / "src" / "legacy_portal_assets.h"
    wm_header = ROOT / "src" / "src" / "WiFiManager" / "legacy_wm_assets.h"
    assert portal_header.read_text(encoding="utf-8") == embed.render_header(portal_sources)
    assert wm_header.read_text(encoding="utf-8") == embed.render_header(wm_sources, external=True)

    portal_raw, portal_deflate = verify_group(header_assets(portal_header), portal_sources)
    wm_raw, wm_deflate = verify_group(header_assets(wm_header), wm_sources)
    menu = wm_sources["myCustMenu"]
    for token, value in ((b"@@REMOTE_VERSION@@", b"7.4.2"), (b"@@REMOTE_VERSION_EXTRA@@", b"-test"), (b"@@REMOTE_WEBHOME@@", b"remote")):
        assert menu.count(token) == 1
        menu = menu.replace(token, value)
    assert b"7.4.2 (-test)" in menu and b"https://remote.out-a-ti.me" in menu
    def active_wm_assets(*, style_50s: bool, param3: bool) -> dict[str, bytes]:
        selected = {
            name: data for name, data in wm_sources.items()
            if not name.endswith("_50S") and not name.endswith("_NOPARAM3") and not name.startswith("HTTP_STYLE_C80")
        }
        suffix = "_50S" if style_50s else ""
        for base in ("HTTP_STYLE", "HTTP_STYLE_UPLF"):
            selected[base] = wm_sources[base + suffix]
        if style_50s:
            selected["HTTP_STYLE_C80"] = wm_sources["HTTP_STYLE_C80_50S"]
        elif param3:
            selected["HTTP_STYLE_C80"] = wm_sources["HTTP_STYLE_C80"]
        else:
            selected["HTTP_STYLE_C80"] = wm_sources["HTTP_STYLE_C80_NOPARAM3"]
        return selected

    savings = []
    for style_50s in (False, True):
        chosen = active_wm_assets(style_50s=style_50s, param3=True)
        raw = portal_raw + sum(map(len, chosen.values()))
        compressed = portal_deflate + sum(len(embed.raw_deflate(data)) for data in chosen.values())
        savings.append(raw - compressed)
    assert min(savings) >= 13_000, savings

    wm_cpp = (ROOT / "src/src/WiFiManager/WiFiManager.cpp").read_text(encoding="utf-8")
    assert "legacyPortalAppendString(page, legacy_HTTP_STYLE_50S_deflate" in wm_cpp
    assert "legacyPortalAppendString(page, legacy_HTTP_STYLE_deflate" in wm_cpp
    assert "legacyPortalAppendString(page, legacy_HTTP_STYLE_C80_50S_deflate" in wm_cpp
    assert "legacyPortalAppendString(page, legacy_HTTP_STYLE_C80_deflate" in wm_cpp
    assert "legacyPortalAppendString(page, legacy_HTTP_STYLE_C80_NOPARAM3_deflate" in wm_cpp
    assert "bufSize += WM_C80_SIZE;" in wm_cpp
    assert "legacyPortalAppendString(page, legacy_myCustMenu_deflate" in wm_cpp
    assert "legacyPortalInsertString(pitem, labelOffset, legacy_crsfModeHelp_deflate" in wm_cpp
    remote_wifi = (ROOT / "src/remote_wifi.cpp").read_text(encoding="utf-8")
    assert "#ifndef ARDUINO_ARCH_ESP32\nstatic const char *myCustMenu = " in remote_wifi
    assert 'static const char *myCustMenu = "__ELRS_REMOTE_MENU__|" UNI_VERSION "|" UNI_VERSION_EXTRA "|" WEBHOME;' in remote_wifi
    assert '"__ELRS_CRSF_MODE_HELP__"' in (ROOT / "src/src/CRSF/crsf_wifi.h").read_text(encoding="utf-8")
    assert "#ifdef HAVE_CRSF" in portal_header.read_text(encoding="utf-8")
    print(
        "legacy portal assets passed: "
        f"portal={portal_raw - portal_deflate}B, "
        f"total active savings={min(savings)}B (CRSF build; styles 50s/standard)"
    )


if __name__ == "__main__":
    main()
