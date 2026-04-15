#!/usr/bin/env python3
# [usage] 
# cmake --build build --target dynaa-check -j $(nproc)
# cmake --build build --target dynaa-instrument -j $(nproc)
# cmake --build build --target Runtime -j $(nproc)
# python3 tools/alias/dynaa/run_dynaa.py [--source <source.c>] [--aa <aa1> <aa2>] [...]
# TODO: for Linux only
import argparse, subprocess, sys, os
from pathlib import Path

def filter_line(line):
    return not line.startswith("[APISpec] Warning:")
def filter_line_1(line):
    # return False
    return not line.startswith("[dynaa-check]")

def main():
    lotus_root = Path(__file__).resolve().parent.parent.parent
    p = argparse.ArgumentParser()
    # ===================== defualt configure =====================
    p.add_argument("--source", default="tmp/test.c")
    p.add_argument("--aa", nargs="+", default=[
        "andersen", # "andersen1", "andersen2", 
         "aser-pta", # "aser-pta-1cfa", "aser-pta-2cfa", 
         "aser-pta-origin", "dda", "tpa", "tpa-1cfa", 
         "tpa-2cfa", "tpa-3cfa", "tpa-5", "dyck", 
         "seadsa",  "cfl-anders", "cfl-steens", 
         "allocaa", "basic", "tbaa", "globals", 
         "scevaa", "sraa", "combined", "underapprox",
         "svfander","svfsteens","svfsfrander","svffspta","svftype"
         # "svfnander","svfsander","svfvfspta"
        ])
    p.add_argument("--clang", default="clang-14")
    p.add_argument("--dynaa-instrument", default=str(lotus_root / "build/bin/dynaa-instrument"))
    p.add_argument("--dynaa-check", default=str(lotus_root / "build/bin/dynaa-check"))
    p.add_argument("--runtime", default=str(lotus_root / "build/libRuntime.a"))
    p.add_argument("--log-dir", default=str(lotus_root / "tmp"))
    p.add_argument("--tmp-dir", default=str(lotus_root / "tmp"))
    p.add_argument("--wpa-dir", default=str(lotus_root.parent / "SVF/Release-build/bin/"))
    # ===================== defualt configure =====================
    args = p.parse_args()
    
    os.makedirs(args.tmp_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)
    bc = Path(args.tmp_dir) / f"{Path(args.source).stem}.bc"
    ll = Path(args.tmp_dir) / f"{Path(args.source).stem}.ll"
    inst = Path(args.tmp_dir) / f"{Path(args.source).stem}.inst.bc"
    exe = Path(args.tmp_dir) / f"{Path(args.source).stem}"
    log = Path(args.log_dir) / "pts.log"

    if p in [bc, ll, inst, exe, log]:
        if p.exists():
            p.unlink()
    if subprocess.run(f"{args.clang} -emit-llvm -c {args.source} -o {bc}", shell=True).returncode:
        print("Error: Compilation to .bc failed")
        sys.exit(1)
    if subprocess.run(f"{args.clang} -emit-llvm -c {args.source} -S -o {ll}", shell=True).returncode:
        print("Error: Compilation to .ll failed")
        sys.exit(1)
    result = subprocess.run(f"{args.dynaa_instrument} {bc} -o {inst}", shell=True, capture_output=True, text=True)
    output = result.stdout + result.stderr
    for line in output.splitlines():
        if filter_line(line):
            print(line)
            if "error:" in line.lower():
                print("Error: Instrumentation failed")
                sys.exit(1)
    if subprocess.run(f"{args.clang} {inst} {args.runtime} -o {exe}", shell=True).returncode:
        print("Error: Linking failed")
        sys.exit(1)
    result = subprocess.run([str(exe)], env=dict(os.environ, LOG_DIR=str(args.log_dir)), capture_output=True, text=True)
    output = result.stdout + result.stderr
    for line in output.splitlines():
        if filter_line(line):
            print(line)
    print(f"Program \"{exe}\" exited with code {result.returncode}")
    
    # Check each AA
    for aa in args.aa:
        result = subprocess.run([args.dynaa_check, str(bc), str(log), aa, "-svf-wpa-temp-dir", args.tmp_dir, "-svf-wpa-path", args.wpa_dir], capture_output=True, text=True)
        output = result.stdout + result.stderr
        for line in output.splitlines():
            if filter_line(line) and not filter_line_1(line):
                print(line)
        if result.returncode != 0:
            print(f"{aa} threw exception")
    
    sys.exit(0)

if __name__ == "__main__":
    main()