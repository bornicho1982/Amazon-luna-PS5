import os
import re

runtime_path = r'tools\luna_probe\bundles\8I4cnFRo9NwF2_B.js_name_runtime'
with open(runtime_path, 'r', encoding='utf-8', errors='ignore') as f:
    runtime_data = f.read()

# Let's inspect how chunks are loaded in runtime
print("Length of runtime:", len(runtime_data))
# Look for chunk mapping or filename logic
matches = re.findall(r'(\d+):"([a-zA-Z0-9_\-\$]+)"', runtime_data)
print(f"Found {len(matches)} chunk entries")
chunk_dict = dict(matches)
if "89316" in chunk_dict:
    print("Found 89316:", chunk_dict["89316"])
for c in ["4359", "4652", "6654", "6015", "89316"]:
    if c in chunk_dict:
        print(f"Chunk {c} -> {chunk_dict[c]}")

# Let's print the chunk filename expression: e.g. "" + e + ".js" or similar
fn_match = re.search(r'function\s*\w*\(e\)\{return[^\}]+\.js[^\}]*\}', runtime_data)
if fn_match:
    print("Chunk filename func:", fn_match.group(0))
