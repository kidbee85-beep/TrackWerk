#!/bin/bash
# usage: run-checks.sh <tree> <builddir> <outroot>   — runs every host check, one line per check.
# ptpresetpack-check gets a manifest of the factory instrument presets (as Codex builds it);
# ptcapture-input-policy-check runs without an argument; the others get a fresh run directory.
TREE=$1; B=$TREE/$2; OUT=$3; mkdir -p "$OUT"
MANIFEST="$OUT/presetpack-manifest.json"
python3 - "$TREE/factory/Presets/Instruments" "$MANIFEST" <<'PY'
import json,sys,pathlib
root=pathlib.Path(sys.argv[1]);out=[]
for p in sorted(root.rglob("*.pti")):
    j=json.loads(p.read_text());ins=j.get("instrument",j)
    out.append({"path":str(p),"name":ins.get("name",""),"category":p.parent.name})
json.dump({"presets":out},open(sys.argv[2],"w"))
PY
for t in $(grep -o 'add_executable(pt[a-z0-9-]*' "$TREE/native/CMakeLists.txt" | sed 's/add_executable(//'); do
  [ "$t" = ptsynth-cpu-bench ] && continue
  [ -x "$B/$t" ] || { echo "$t | nicht gebaut"; continue; }
  d="$OUT/$t"; rm -rf "$d"; mkdir -p "$d"
  case $t in
    pteffect-collection-check) args=("$TREE/factory/Presets" "$d/out");;
    ptpresetpack-check) args=("$MANIFEST" "$d/out");;
    ptcapture-input-policy-check|ptfft-roundtrip-check) args=();;
    *) args=("$d/out");;
  esac
  (cd "$d" && "$B/$t" "${args[@]}" > "$d.log" 2>&1); rc=$?
  echo "$t | rc=$rc | $(tail -1 "$d.log" | cut -c1-110)"
done
