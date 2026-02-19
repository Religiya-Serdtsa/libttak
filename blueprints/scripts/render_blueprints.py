#!/usr/bin/env python3
import os
import sys
import zlib
import urllib.request
import subprocess
from pathlib import Path

# PlantUML encoding for the online server
def encode_puml(puml_text: str) -> str:
    """Encodes PlantUML text for use with the online server."""
    # 1) UTF-8 encode
    data = puml_text.encode("utf-8")
    # 2) Deflate compression (PlantUML expects raw DEFLATE stream)
    compressed = zlib.compress(data)[2:-4]
    # 3) Custom Base64 mapping
    return encode_64(compressed)

def encode_64(data: bytes) -> str:
    """Custom Base64 encoding used by PlantUML."""
    res = ""
    for i in range(0, len(data), 3):
        if i + 2 < len(data):
            res += encode_3bytes(data[i], data[i + 1], data[i + 2])
        elif i + 1 < len(data):
            res += encode_3bytes(data[i], data[i + 1], 0)
        else:
            res += encode_3bytes(data[i], 0, 0)
    return res

def encode_3bytes(b1: int, b2: int, b3: int) -> str:
    c1 = b1 >> 2
    c2 = ((b1 & 0x3) << 4) | (b2 >> 4)
    c3 = ((b2 & 0xF) << 2) | (b3 >> 6)
    c4 = b3 & 0x3F
    res = ""
    for c in (c1, c2, c3, c4):
        res += encode_6bit(c & 0x3F)
    return res

def encode_6bit(b: int) -> str:
    if b < 10:
        return chr(48 + b)
    b -= 10
    if b < 26:
        return chr(65 + b)
    b -= 26
    if b < 26:
        return chr(97 + b)
    b -= 26
    if b == 0:
        return "-"
    if b == 1:
        return "_"
    return "?"

def inject_layout_guards(puml_text: str) -> str:
    """
    Injects layout-related skinparams to reduce curvy, spline-like edges.

    Rationale:
      - Many PlantUML diagram types (component/class/etc.) are routed by Graphviz.
      - Graphviz defaults can create curved splines when the graph is dense.
      - 'skinparam linetype ortho' forces orthogonal routing when supported.
    """
    # Do not blindly duplicate if user already configured these explicitly.
    wanted_lines = [
        "skinparam linetype ortho",
        # Keep arrows visually crisp; higher DPI makes bends look cleaner.
        "skinparam dpi 160",
        # Slightly reduce diagonal bias; helps readability in dense graphs.
        "skinparam ArrowThickness 1",
        "skinparam DefaultTextAlignment left",
    ]

    # Quick scan to see what is already present (case-insensitive).
    lower = puml_text.lower()
    already_has = set()
    for line in wanted_lines:
        key = line.split()[1] if line.startswith("skinparam ") else line
        if line.lower() in lower:
            already_has.add(line.lower())

    inject = [ln for ln in wanted_lines if ln.lower() not in already_has]
    if not inject:
        return puml_text

    lines = puml_text.splitlines()

    # Insert right after the first @startuml (preferred) to make it global for the diagram.
    out = []
    inserted = False
    for idx, line in enumerate(lines):
        out.append(line)
        if not inserted and line.strip().lower().startswith("@startuml"):
            # Add a blank line for readability
            out.append("")
            out.extend(inject)
            out.append("")
            inserted = True

    # If @startuml was missing, do nothing (keep original text).
    if not inserted:
        return puml_text

    return "\n".join(out) + ("\n" if not puml_text.endswith("\n") else "")

def try_run_pipe_png(cmd: list[str], puml_text: str, output_path: Path) -> bool:
    """
    Runs PlantUML in pipe mode and writes PNG bytes to output_path.
    This guarantees our injected skinparams are used without touching the .puml file.
    """
    try:
        proc = subprocess.run(
            cmd,
            input=puml_text.encode("utf-8"),
            check=True,
            capture_output=True,
        )
        # PlantUML emits PNG to stdout in -pipe -tpng mode.
        output_path.write_bytes(proc.stdout)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False

def render_locally(puml_text: str, output_path: Path) -> bool:
    """Attempts to render using local plantuml command or a local plantuml.jar."""
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Prefer pipe mode to avoid file path quirks and to enforce our preprocessing.
    if try_run_pipe_png(["plantuml", "-pipe", "-tpng"], puml_text, output_path):
        return True

    # Fallback: use java -jar plantuml.jar if found.
    jar_paths = [
        Path(__file__).parent / "plantuml.jar",
        Path(__file__).parent.parent / "plantuml.jar",
        Path.home() / "plantuml.jar",
    ]
    for jar in jar_paths:
        if jar.exists():
            if try_run_pipe_png(["java", "-jar", str(jar), "-pipe", "-tpng"], puml_text, output_path):
                return True

    return False

def render_remotely(puml_text: str, output_path: Path) -> bool:
    """Renders using the official PlantUML online server."""
    print(f"  Rendering via PlantUML server...")
    encoded = encode_puml(puml_text)

    # Keep as http like your original script; switch to https if you prefer.
    url = f"http://www.plantuml.com/plantuml/png/{encoded}"

    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req) as response:
            output_path.write_bytes(response.read())
        return True
    except Exception as e:
        print(f"  Error rendering remotely: {e}")
        return False

def main() -> None:
    # Setup paths
    base_dir = Path(__file__).parent.parent.absolute()
    puml_dir = base_dir
    png_dir = base_dir / "png"
    png_dir.mkdir(parents=True, exist_ok=True)

    puml_files = sorted(puml_dir.rglob("*.puml"))
    if not puml_files:
        print("No .puml files found in blueprints directory.")
        return

    print(f"Found {len(puml_files)} .puml files. Checking for updates...")

    updated_count = 0
    for puml_path in puml_files:
        relative_path = puml_path.relative_to(puml_dir)
        png_path = png_dir / relative_path.with_suffix(".png")

        # Re-render only when source is newer than output.
        if png_path.exists() and png_path.stat().st_mtime > puml_path.stat().st_mtime:
            continue

        print(f"Processing {relative_path}...")

        puml_text = puml_path.read_text(encoding="utf-8")
        # Inject orthogonal routing and related params to reduce curvy edges.
        puml_text = inject_layout_guards(puml_text)

        # Try local first, then remote
        success = render_locally(puml_text, png_path)
        if not success:
            success = render_remotely(puml_text, png_path)

        if success:
            updated_count += 1
        else:
            print(f"Failed to render {puml_path.name}")

    if updated_count > 0:
        print(f"Successfully updated {updated_count} blueprints in {png_dir}")
    else:
        print("All blueprints are up to date.")

if __name__ == "__main__":
    main()
