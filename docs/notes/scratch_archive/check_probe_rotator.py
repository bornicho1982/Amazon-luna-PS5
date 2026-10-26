#!/usr/bin/env python3
import json

with open("tools/luna_probe/luna_probe.jsonl", "r", encoding="utf-8") as f:
    for line in f:
        try:
            obj = json.loads(line)
            s = json.dumps(obj)
            if "featured_rotator" in s.lower() or "rotator" in s.lower():
                print("Found match in luna_probe.jsonl:")
                # print keys or snippet
                print(s[:500])
        except Exception:
            pass
