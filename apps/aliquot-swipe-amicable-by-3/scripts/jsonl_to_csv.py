import json
import csv
import os
import sys

def convert_jsonl_to_csv(jsonl_filepath, csv_filepath):
    """
    Converts a JSONL file to a CSV file.
    Assumes all JSON objects in the file have the same keys for CSV header.
    """
    data = []
    keys = set()

    # Read JSONL and collect all unique keys
    try:
        with open(jsonl_filepath, 'r', encoding='utf-8') as f:
            for line in f:
                if line.strip():
                    try:
                        obj = json.loads(line)
                        data.append(obj)
                        keys.update(obj.keys())
                    except json.JSONDecodeError as e:
                        print(f"Error decoding JSON from line: {line.strip()} - {e}", file=sys.stderr)
                        continue
    except FileNotFoundError:
        print(f"Error: JSONL file not found at {jsonl_filepath}", file=sys.stderr)
        return False
    except Exception as e:
        print(f"Error reading JSONL file: {e}", file=sys.stderr)
        return False

    if not data:
        print(f"No data found in {jsonl_filepath} to convert.", file=sys.stderr)
        return False

    # Sort keys for consistent header order
    sorted_keys = sorted(list(keys))

    # Write to CSV
    try:
        with open(csv_filepath, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=sorted_keys)
            writer.writeheader()
            writer.writerows(data)
        print(f"Successfully converted {jsonl_filepath} to {csv_filepath}")
        return True
    except Exception as e:
        print(f"Error writing CSV file: {e}", file=sys.stderr)
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage: python jsonl_to_csv.py <input_jsonl_file> [output_csv_file]", file=sys.stderr)
        print("If output_csv_file is not provided, it defaults to <input_jsonl_file>.csv", file=sys.stderr)
        sys.exit(1)

    input_jsonl = sys.argv[1]
    
    # Ensure input_jsonl is treated relative to /opt/aliquot-3
    OPT_DIR = "/opt/aliquot-3"
    if not os.path.isabs(input_jsonl):
        # If it's a relative path, assume it's relative to OPT_DIR
        input_jsonl_full_path = os.path.join(OPT_DIR, input_jsonl)
    elif not input_jsonl.startswith(OPT_DIR):
        # If it's an absolute path but not under OPT_DIR, assume it's a file name
        # and search for it under OPT_DIR. This might be ambiguous but enforces the rule.
        input_jsonl_full_path = os.path.join(OPT_DIR, os.path.basename(input_jsonl))
    else:
        # It's already an absolute path under OPT_DIR
        input_jsonl_full_path = input_jsonl

    if len(sys.argv) == 3:
        output_csv = sys.argv[2]
    else:
        base, _ = os.path.splitext(input_jsonl_full_path)
        output_csv = f"{base}.csv"

    convert_jsonl_to_csv(input_jsonl_full_path, output_csv)
