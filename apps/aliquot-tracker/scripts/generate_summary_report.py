import json
import os
import sys
import datetime

def generate_summary_report(jsonl_filepath, report_filepath, app_name="ALIQUOT"):
    """
    Generates a fixed-width summary report from a JSONL file.
    It expects 'found' type records (e.g., from aliquot_found.jsonl).
    """
    
    data = []
    try:
        with open(jsonl_filepath, 'r', encoding='utf-8') as f:
            for line in f:
                if line.strip():
                    try:
                        obj = json.loads(line)
                        data.append(obj)
                    except json.JSONDecodeError as e:
                        print(f"Error decoding JSON from line in {jsonl_filepath}: {e}", file=sys.stderr)
                        continue
    except FileNotFoundError:
        print(f"Warning: JSONL file not found at {jsonl_filepath}. No report generated.", file=sys.stderr)
        return False
    except Exception as e:
        print(f"Error reading JSONL file {jsonl_filepath}: {e}", file=sys.stderr)
        return False

    if not data:
        print(f"No data found in {jsonl_filepath} to generate report.", file=sys.stderr)
        return False

    # Sort data by seed to ensure consistent reporting order
    data.sort(key=lambda x: int(x.get('seed', '0')))

    # Determine reporting parameters
    total_seeds_processed = len(data)
    
    # Initialize values for summary
    first_seed = data[0].get('seed', 'N/A')
    last_seed = data[-1].get('seed', 'N/A')
    max_value_seen = 0
    max_value_seed = 'N/A'
    
    found_sociable_3_count = 0
    found_amicable_count = 0
    found_perfect_count = 0
    found_cycle_count = 0
    found_terminated_count = 0
    found_overflow_count = 0

    for item in data:
        current_max_val = int(item.get('max', '0'))
        if current_max_val > max_value_seen:
            max_value_seen = current_max_val
            max_value_seed = item.get('seed', 'N/A')
            
        status = item.get('status', 'unknown')
        if status == 'sociable-3':
            found_sociable_3_count += 1
        elif status == 'amicable':
            found_amicable_count += 1
        elif status == 'perfect':
            found_perfect_count += 1
        elif 'cycle' in status: # Catch 'cycle' and 'big-cycle'
            found_cycle_count += 1
        elif 'terminated' in status: # Catch 'terminated' and 'big-terminated'
            found_terminated_count += 1
        elif status == 'overflow':
            found_overflow_count += 1


    # Write report
    try:
        with open(report_filepath, 'w', encoding='utf-8') as f:
            f.write(f"--- {app_name} Summary Report ---
")
            f.write(f"Generated from: {os.path.basename(jsonl_filepath)}
")
            f.write(f"Timestamp: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
")
            f.write(f"-----------------------------------

")

            f.write(f"Scan Range: {first_seed.ljust(15)} to {last_seed.ljust(15)}
")
            f.write(f"Total Seeds Recorded: {total_seeds_processed}
")
            f.write(f"Max Value Seen: {str(max_value_seen).ljust(20)} (from seed {max_value_seed})

")

            f.write(f"Findings Summary:
")
            if found_sociable_3_count > 0:
                f.write(f"  Sociable-3 Numbers: {found_sociable_3_count}
")
            if found_amicable_count > 0:
                f.write(f"  Amicable Pairs:   {found_amicable_count}
")
            if found_perfect_count > 0:
                f.write(f"  Perfect Numbers:  {found_perfect_count}
")
            if found_cycle_count > 0:
                f.write(f"  Other Cycles:     {found_cycle_count}
")
            if found_terminated_count > 0:
                f.write(f"  Terminated:       {found_terminated_count}
")
            if found_overflow_count > 0:
                f.write(f"  Overflows:        {found_overflow_count}
")
            
            f.write(f"
-----------------------------------
")
            print(f"Successfully generated summary report: {report_filepath}")
        return True
    except Exception as e:
        print(f"Error writing summary report {report_filepath}: {e}", file=sys.stderr)
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2 or len(sys.argv) > 4:
        print("Usage: python generate_summary_report.py <input_jsonl_file> [output_report_file] [app_name]", file=sys.stderr)
        print("If output_report_file is not provided, it defaults to <input_jsonl_file>_report.txt", file=sys.stderr)
        sys.exit(1)

    input_jsonl = sys.argv[1]
    
    # Ensure input_jsonl is treated relative to /opt/aliquot-tracker
    OPT_DIR = "/opt/aliquot-tracker"
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
    
    if len(sys.argv) == 4:
        output_report = sys.argv[2]
        app_name = sys.argv[3]
    elif len(sys.argv) == 3:
        output_report = sys.argv[2]
        app_name = "ALIQUOT" # Default
    else:
        base, _ = os.path.splitext(input_jsonl_full_path)
        output_report = f"{base}_report.txt"
        app_name = "ALIQUOT" # Default

    generate_summary_report(input_jsonl_full_path, output_report, app_name)
