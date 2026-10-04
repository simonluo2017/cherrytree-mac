#!/usr/bin/env python3
"""Pin size_bytes and sha256 of every model in data/models.manifest.

Queries the Hugging Face API (https://huggingface.co/api/models/<repo>/tree/main)
for the LFS metadata of each listed file and writes the values back into the
manifest, so that the application verifies downloads against hashes that are
committed in the repository rather than fetched at download time.

Usage:  python3 scripts/update_model_manifest.py [path/to/models.manifest]
Needs only the Python standard library; run it on a machine that can reach
huggingface.co, review the diff, commit.
"""
import configparser
import json
import sys
import urllib.request

path = sys.argv[1] if len(sys.argv) > 1 else "data/models.manifest"
cfg = configparser.ConfigParser(interpolation=None)
cfg.optionxform = str
with open(path, encoding="utf-8") as f:
    header = [line for line in f if line.startswith("#")]
cfg.read(path, encoding="utf-8")

def lfs_of(entries, filename):
    entry = next((e for e in entries if e.get("path") == filename), None)
    if not entry or "lfs" not in entry:
        return None
    return entry["lfs"]

for section in cfg.sections():
    repo, filename = cfg[section]["repo"], cfg[section]["file"]
    url = f"https://huggingface.co/api/models/{repo}/tree/main"
    with urllib.request.urlopen(url, timeout=60) as resp:
        entries = json.load(resp)
    lfs = lfs_of(entries, filename)
    if not lfs:
        print(f"!! {section}: {filename} not found in {repo}; files there: "
              + ", ".join(e.get("path", "") for e in entries if e.get("path", "").endswith(".gguf")), file=sys.stderr)
        continue
    cfg[section]["size_bytes"] = str(lfs["size"])
    cfg[section]["sha256"] = lfs["oid"]
    print(f"{section}: {lfs['size']} bytes sha256={lfs['oid']}")
    extra = [f.strip() for f in cfg[section].get("files_extra", "").split(";") if f.strip()]
    if extra:
        sizes, hashes = [], []
        for part in extra:
            plfs = lfs_of(entries, part)
            if not plfs:
                print(f"!! {section}: extra part {part} not found", file=sys.stderr)
                sizes.append("0"); hashes.append("")
                continue
            sizes.append(str(plfs["size"])); hashes.append(plfs["oid"])
            print(f"   {part}: {plfs['size']} bytes sha256={plfs['oid']}")
        cfg[section]["size_bytes_extra"] = ";".join(sizes)
        cfg[section]["sha256_extra"] = ";".join(hashes)

with open(path, "w", encoding="utf-8") as f:
    f.writelines(header)
    f.write("\n")
    cfg.write(f, space_around_delimiters=False)
