#!/usr/bin/env python3
"""Convert the radar-dash HTML page to a C header for embedding in firmware.

Source selection:
  1. Prefer ``../radar-dash/index.html`` (the sibling repo in the monorepo).
  2. Fall back to the local vendored copy at ``src/radar_page.html`` for
     standalone clones of ``mr60bha2-radar``.

Either way the firmware serves radar-dash at ``http://<ESP-IP>/``, which
auto-connects to ``ws://<ESP-IP>/ws`` when no ``?ws=`` override is given.
"""
import os
import sys

def main():
    src_dir = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(src_dir, "radar_page.h")

    # Preferred source: the sibling radar-dash repo in the monorepo.
    monorepo_html = os.path.abspath(
        os.path.join(src_dir, "..", "..", "radar-dash", "index.html")
    )
    # Fallback: the vendored copy inside this repo.
    vendored_html = os.path.join(src_dir, "radar_page.html")

    if os.path.isfile(monorepo_html):
        html_path = monorepo_html
        source_label = "monorepo radar-dash/index.html"
    elif os.path.isfile(vendored_html):
        html_path = vendored_html
        source_label = "vendored src/radar_page.html (fallback)"
    else:
        print(
            "ERROR: no HTML source found. Expected one of:\n"
            f"  {monorepo_html}\n"
            f"  {vendored_html}",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"embed_html: using {source_label} -> {html_path}")

    with open(html_path, "r", encoding="utf-8") as f:
        html = f.read()

    html_bytes = html.encode("utf-8")

    with open(header_path, "w") as f:
        f.write("/* AUTO-GENERATED from radar-dash index.html - do not edit */\n")
        f.write(f"/* source: {source_label} */\n")
        f.write("#pragma once\n\n")
        f.write(f"static const unsigned int radar_page_html_len = {len(html_bytes)};\n")
        f.write("static const char radar_page_html[] = {\n")

        for i, b in enumerate(html_bytes):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{b:02x},")
            if i % 16 == 15:
                f.write("\n")
            else:
                f.write(" ")

        f.write("\n    0x00  /* null terminator */\n};\n")

    print(f"Generated {header_path} ({len(html_bytes)} bytes)")

if __name__ == "__main__":
    main()
