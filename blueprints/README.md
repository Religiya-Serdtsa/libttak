# Blueprints

This directory contains the architecture blueprints for `libttak` in PlantUML format (`.puml`).

## Directory Structure

- `*.puml`: PlantUML source files.
- `png/`: Automatically generated PNG images of the blueprints.
- `scripts/`: Conversion scripts.

## Rendering Blueprints

To update the PNG files from the `.puml` sources, run the following command from the project root:

```bash
make blueprints
```

Or run the script directly:

```bash
python3 blueprints/scripts/render_blueprints.py
```

The script will automatically check for changes and only re-render files that have been modified. It attempts to use a local `plantuml` installation first, and falls back to the PlantUML online server if necessary.

## Adding New Blueprints

1. Create a new `.puml` file in this directory.
2. Run `make blueprints`.
3. The resulting PNG will be saved in `blueprints/png/`.
