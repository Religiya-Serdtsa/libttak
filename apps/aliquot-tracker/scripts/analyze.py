import json
import os
import requests
import time

# --- Configuration ---
STATE_DIR = "/opt/aliquot-tracker"
FOUND_LOG = os.path.join(STATE_DIR, "aliquot_found.jsonl")
TRACK_LOG = os.path.join(STATE_DIR, "aliquot_track.jsonl")
FINAL_REPORT = "aliquot_true_peak_report.txt"

class PeakReconstructor:
    def __init__(self):
        self.tracking_db = self._load_tracking_data()

    def _load_tracking_data(self):
        db = {}
        if os.path.exists(TRACK_LOG):
            with open(TRACK_LOG, 'r') as f:
                for line in f:
                    try:
                        d = json.loads(line)
                        db[d["seed"]] = d
                    except: continue
        return db

    def reconstruct_peak(self, bits, prefix_str):
        """
        Reconstructs the approximate BigInt peak using bit-length and prefix.
        Formula: prefix * 10^(log10(2^bits) - len(prefix) + 1)
        """
        if not prefix_str or prefix_str == "N/A":
            return "Unknown (Missing Prefix)"
        
        # log10(2^bits) tells us how many decimal digits total
        total_decimal_digits = int(bits * 0.30103)
        remaining_digits = total_decimal_digits - len(prefix_str) + 1
        
        if remaining_digits < 0: # Small numbers
            return prefix_str
            
        # Append zeros to represent the magnitude
        return prefix_str + ("0" * remaining_digits) + f" (Approx. {bits} bits)"

    def run(self):
        if not os.path.exists(FOUND_LOG):
            print(f"Error: {FOUND_LOG} not found.")
            return

        report = [
            "============================================================",
            "   ALIQUOT TRUE PEAK RECONSTRUCTION REPORT (Mersenne Ready)  ",
            "============================================================",
            "Note: Peaks for >64-bit sequences are reconstructed from Prefix/Bits.",
            "------------------------------------------------------------\n"
        ]

        print(f"{'Seed':<10} | {'Bits':<4} | {'Status':<12} | {'True Peak Insight'}")
        print("-" * 80)

        with open(FOUND_LOG, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line)
                    seed = entry['seed']
                    track = self.tracking_db.get(seed, {})
                    
                    bits = track.get("bits", int(entry['max']).bit_length())
                    prefix = track.get("prefix", "N/A")
                    
                    # Core Reconstruction Logic
                    if bits > 64:
                        true_peak = self.reconstruct_peak(bits, prefix)
                    else:
                        true_peak = f"{entry['max']:,}"

                    report.append(f"Seed: {seed}")
                    report.append(f"  Status    : {entry['status'].upper()}")
                    report.append(f"  Steps     : {entry['steps']}")
                    report.append(f"  RECON PEAK: {true_peak}")
                    report.append(f"  Magnitude : {bits} bits")
                    report.append("-" * 60)

                    print(f"{seed:<10} | {bits:<4} | {entry['status'][:12]:<12} | {prefix[:10]}... ({bits}b)")

                except Exception: continue

        with open(FINAL_REPORT, 'w') as out_f:
            out_f.write("\n".join(report))
        print(f"\n[Success] True Peak report saved to {FINAL_REPORT}")

if __name__ == "__main__":
    PeakReconstructor().run()
