#!/usr/bin/env python3
"""Lists all translatable texts of the interface (keys of _(), R_() and N_()) in i18n/keys.txt,
numbered. Translations live in i18n/<lang>.txt as "number|text" lines; gen.py builds app/lang_*.c."""
import re, glob, os
root = os.path.dirname(os.path.abspath(__file__))
keys = []
extra = ["Customer no.", "Contact", "System name", "Device type", "Started", "Finished", "Passes", "HPA / DCO", "Software", "CUSTOMER", "DEVICE", "VERIFIED DRIVES", "ERASED DRIVES", "VERIFICATION", "ERASURE", "Desktop", "Laptop", "Handheld", "All-in-one", "Server", "Tablet", "Embedded PC", "Computer", "none", "unknown", "Unknown drive"]
for f in sorted(glob.glob(os.path.join(root, "../app/*.c"))):
    if os.path.basename(f).startswith(("lang_", "f_", "pdfgen", "logo_data")):
        continue
    src = open(f, encoding="utf-8").read()
    for m in re.finditer(r'\b(?:_|R_|N_)\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)', src):
        lit = "".join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', m.group(1)))
        if lit not in keys:
            keys.append(lit)
for k in extra:
    if k not in keys:
        keys.append(k)
old = []
kp = os.path.join(root, "keys.txt")
if os.path.exists(kp):
    old = [l.rstrip("\n").split("|", 1)[1] for l in open(kp, encoding="utf-8") if "|" in l]
# keep existing numbers stable, append new keys
out = [k for k in old if k in keys] if False else old[:]
for k in keys:
    if k not in out:
        out.append(k)
with open(kp, "w", encoding="utf-8") as fp:
    for i, k in enumerate(out, 1):
        fp.write(f"{i}|{k}\n")
print(len(out), "keys,", len([k for k in out if k not in keys]), "no longer used")
