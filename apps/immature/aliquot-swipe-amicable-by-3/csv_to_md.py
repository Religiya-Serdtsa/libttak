import csv
import sys

if len(sys.argv) != 2:
    print("usage: python3 csv_to_md.py input.csv")
    sys.exit(1)

path = sys.argv[1]

with open(path, newline="") as f:
    rows = list(csv.reader(f))

if not rows:
    sys.exit(0)

header = rows[0]
data = rows[1:]

widths = [len(h) for h in header]
for row in data:
    for i, cell in enumerate(row):
        widths[i] = max(widths[i], len(cell))

def fmt(row):
    return "| " + " | ".join(cell.ljust(widths[i]) for i, cell in enumerate(row)) + " |"

print(fmt(header))
print("|-" + "-|-".join("-" * w for w in widths) + "-|")
for row in data:
    print(fmt(row))

