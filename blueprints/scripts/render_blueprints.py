#!/usr/bin/env python3
import os
import sys
import zlib
import urllib.request
import subprocess
from pathlib import Path

# PlantUML encoding for the online server
def encode_puml(puml_text):
    """Encodes PlantUML text for use with the online server."""
    # 1. UTF-8 encode
    data = puml_text.encode('utf-8')
    # 2. Deflate compression
    compressed = zlib.compress(data)[2:-4]
    # 3. Custom Base64 mapping
    return encode_64(compressed)

def encode_64(data):
    """Custom Base64 encoding used by PlantUML."""
    res = ""
    for i in range(0, len(data), 3):
        if i + 2 < len(data):
            res += encode_3bytes(data[i], data[i+1], data[i+2])
        elif i + 1 < len(data):
            res += encode_3bytes(data[i], data[i+1], 0)
        else:
            res += encode_3bytes(data[i], 0, 0)
    return res

def encode_3bytes(b1, b2, b3):
    c1 = b1 >> 2
    c2 = ((b1 & 0x3) << 4) | (b2 >> 4)
    c3 = ((b2 & 0xF) << 2) | (b3 >> 6)
    c4 = b3 & 0x3F
    res = ""
    for c in [c1, c2, c3, c4]:
        res += encode_6bit(c & 0x3F)
    return res

def encode_6bit(b):
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
        return '-'
    if b == 1:
        return '_'
    return '?'

def render_locally(puml_path, output_path):
    """Attempts to render using local plantuml command."""
    try:
        # Try 'plantuml' command
        subprocess.run(['plantuml', '-tpng', str(puml_path), '-o', str(output_path.parent)], 
                       check=True, capture_output=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Try java -jar plantuml.jar if it exists in the same directory as script or blueprints
        jar_paths = [
            Path(__file__).parent / 'plantuml.jar',
            Path(__file__).parent.parent / 'plantuml.jar',
            Path.home() / 'plantuml.jar'
        ]
        for jar in jar_paths:
            if jar.exists():
                try:
                    subprocess.run(['java', '-jar', str(jar), '-tpng', str(puml_path), '-o', str(output_path.parent)],
                                   check=True, capture_output=True)
                    return True
                except subprocess.CalledProcessError:
                    continue
    return False

def render_remotely(puml_path, output_path):
    """Renders using the official PlantUML online server."""
    print(f"  Rendering {puml_path.name} via PlantUML server...")
    with open(puml_path, 'r', encoding='utf-8') as f:
        puml_text = f.read()
    
    encoded = encode_puml(puml_text)
    url = f"http://www.plantuml.com/plantuml/png/{encoded}"
    
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as response:
            with open(output_path, 'wb') as out_file:
                out_file.write(response.read())
        return True
    except Exception as e:
        print(f"  Error rendering {puml_path.name} remotely: {e}")
        return False

def main():
    # Setup paths
    base_dir = Path(__file__).parent.parent.absolute()
    puml_dir = base_dir
    png_dir = base_dir / 'png'
    
    if not png_dir.exists():
        png_dir.mkdir(parents=True)

    puml_files = list(puml_dir.glob('*.puml'))
    if not puml_files:
        print("No .puml files found in blueprints directory.")
        return

    print(f"Found {len(puml_files)} .puml files. Checking for updates...")
    
    updated_count = 0
    for puml_path in puml_files:
        png_path = png_dir / (puml_path.stem + '.png')
        
        # Check if re-render is needed (timestamp comparison)
        if png_path.exists() and png_path.stat().st_mtime > puml_path.stat().st_mtime:
            continue

        print(f"Processing {puml_path.name}...")
        
        # Try local first, then remote
        success = render_locally(puml_path, png_path)
        if not success:
            success = render_remotely(puml_path, png_path)
        
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
