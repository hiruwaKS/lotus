#!/usr/bin/env python3
# [usage] 
# cmake --build build --target dynaa-check -j $(nproc)
# cmake --build build --target dynaa-instrument -j $(nproc)
# cmake --build build --target Runtime -j $(nproc)
# llvm-dis-14 "/home/hrwks/llvm-dev/lotus/tmp/target.inst.bc" -o tmp/output.ll
# python3 tools/alias/dynaa/run_dyncg.py <target.bc>
# TODO: for Linux only
import argparse, subprocess, sys, os
from pathlib import Path

def filter_line(line):
    return not line.startswith("[APISpec] Warning:") and not line.startswith("[AliasSpecManager] Warning:")
def main():
    lotus_root = Path(__file__).resolve().parent.parent.parent
    p = argparse.ArgumentParser()
    # ===================== defualt configure =====================
    p.add_argument('target_bc')
    p.add_argument("--dynaa-instrument", default=str(lotus_root / "build/bin/dynaa-instrument"))
    p.add_argument("--dynaa-check", default=str(lotus_root / "build/bin/dynaa-check"))
    p.add_argument("--runtime", default=str(lotus_root / "build/libRuntime.a"))
    p.add_argument("--log-dir", default=str(lotus_root / "tmp"))
    p.add_argument("--tmp-dir", default=str(lotus_root / "tmp"))
    # ===================== defualt configure =====================
    args = p.parse_args()
    
    os.makedirs(args.tmp_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)
    inst = Path(args.tmp_dir) / f"{Path(args.target_bc).stem}.inst.bc"
    exe = Path(args.tmp_dir) / f"{Path(args.target_bc).stem}"
    log = Path(args.log_dir) / "pts.log"

    result = subprocess.run(f"{args.dynaa_instrument} {args.target_bc} -o {inst}", shell=True, capture_output=True, text=True)
    output = result.stdout + result.stderr
    for line in output.splitlines():
        if filter_line(line):
            print(line)
            if "error:" in line.lower():
                print("[log] [run_dyncg] Error: Instrumentation failed")
                exit(-1)
    print(f"[log] [run_dyncg] instrumented \"{args.target_bc}\" to \"{inst}\".")
    result= subprocess.run(["clang-14", inst, args.runtime, "-lstdc++", "-o", f"{exe}"], capture_output=True, text=True)
    output =result.stdout+result.stderr
    print(output)
    if result.returncode:
        print("[log] [run_dyncg] Error: Linking failed")
        exit(-1)
    print(f"[log] [run_dyncg] bc \"{inst}\" linked to \"{exe}\". Running {exe}...")

    result = subprocess.run([str(exe)], env=dict(os.environ, LOG_DIR=str(args.log_dir)), capture_output=True, text=True)
    output = result.stdout + result.stderr
    for line in output.splitlines():
        if filter_line(line):
            print(line)
    if result.returncode:
        print("[log] [run_dyncg] Error: run failed")
        exit(-1)
    print(f"[log] [run_dyncg] Program \"{exe}\" exited with code {result.returncode}")
    result = subprocess.run([args.dynaa_check, str(args.target_bc), str(log), "none"], capture_output=True, text=True)
    output = result.stdout + result.stderr
    print(f"[log] [run_dyncg] dynaa_check exited with code {result.returncode}, logs are as follows:")
    for line in output.splitlines():
        print(line)

if __name__ == "__main__":
    main()
