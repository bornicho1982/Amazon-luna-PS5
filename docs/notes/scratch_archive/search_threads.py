#!/usr/bin/env python3
import os
import re

root = "apps/luna-ps5/src"
for dirpath, _, filenames in os.walk(root):
    for fn in filenames:
        if fn.endswith((".cpp", ".h", ".c")):
            path = os.path.join(dirpath, fn)
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
            for line_no, line in enumerate(content.splitlines(), 1):
                if "std::thread" in line or "pthread_create" in line or "scePthreadCreate" in line or "g_httpMutex" in line or "recursive_mutex" in line:
                    print(f"{path}:{line_no}: {line.strip()}")
