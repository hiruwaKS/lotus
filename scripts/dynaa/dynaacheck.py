from private_string import TESTS_PATH, LOTUS_PATH, SVF_PATH, PHASAR_PATH

import os
from pathlib import Path
import shutil
import re
import sys
python_executable = "python3"
tmp_dir = os.path.dirname(os.path.abspath(__file__))
ll_file = os.path.join(tmp_dir, "target.ll")
bc_file = os.path.join(tmp_dir, "target.bc")
clang = "/usr/bin/clang-14"
dyncg = os.path.join(LOTUS_PATH, "scripts/dynaa/run_dyncg.py")
wpa_path = os.path.join(SVF_PATH, "Release-build/bin/wpa")
aser_aa = os.path.join(LOTUS_PATH, "build/bin/aser-aa")
cclyzer_path = os.path.join(LOTUS_PATH, "build/bin/cclyzer-aa")
# export LLVM_SYMBOLIZER_PATH=/usr/lib/llvm-14/bin/llvm-symbolizer

phasar_cli_path = os.path.join(PHASAR_PATH, "build/tools/phasar-cli/phasar-cli")
configs = [
    # ("SVF", "FSPTA (very simple)",         f"-fspta -ff-eq-base -dump-callgraph"),
    # ("SVF", "FSPTA (simple)",         f"-fspta -ff-eq-base -svf-main -dump-callgraph"),
    # ("SVF", "FSPTA (vt, pwc)",         f"-fspta  -merge-pwc -vt-in-ir -ff-eq-base  -svf-main -dump-callgraph"),
    # ("SVF", "FSPTA (consts, arrays)",  f"-model-consts -model-arrays -fspta  -merge-pwc -vt-in-ir -ff-eq-base -svf-main -dump-callgraph"),
    # ("SVF", "Andersen (very simple)",        f"-ander -ff-eq-base -dump-callgraph"),
    # ("SVF", "Andersen (simple)",        f"-ander -ff-eq-base -svf-main -dump-callgraph"),
    # ("SVF", "Andersen (vt, pwc)",        f"-ander -merge-pwc -vt-in-ir -ff-eq-base -svf-main -dump-callgraph"),
    # ("SVF", "Andersen (consts, arrays)", f"-ander -model-consts -model-arrays -merge-pwc -vt-in-ir -ff-eq-base -svf-main -dump-callgraph"),
    # ("AserPTA", "default", None),
    # ("Phasar", "CHA CFLAnders", ["--call-graph-analysis", "cha", "--alias-analysis", "cflanders", "--emit-cg-as-dot"]),
    # ("Phasar", "RTA CFLAnders", ["--call-graph-analysis", "rta", "--alias-analysis", "cflanders", "--emit-cg-as-dot"]),
    # ("Phasar", "VTA CFLAnders", ["--call-graph-analysis", "vta", "--alias-analysis", "cflanders", "--emit-cg-as-dot"]),
    # ("Phasar", "OTF CFLAnders", ["--call-graph-analysis", "otf", "--alias-analysis", "cflanders", "--emit-cg-as-dot"]),
    # ("Phasar", "CHA CFLSteens", ["--call-graph-analysis", "cha", "--alias-analysis", "cflsteens", "--emit-cg-as-dot"]),
    # ("Phasar", "RTA CFLSteens", ["--call-graph-analysis", "rta", "--alias-analysis", "cflsteens", "--emit-cg-as-dot"]),
    # ("Phasar", "VTA CFLSteens", ["--call-graph-analysis", "vta", "--alias-analysis", "cflsteens", "--emit-cg-as-dot"]),
    # ("Phasar", "OTF CFLSteens", ["--call-graph-analysis", "otf", "--alias-analysis", "cflsteens", "--emit-cg-as-dot"]),
    ("Cclyzer", "unification", ["--print-cg", "--datalog-analysis=unification"]),
    ("Cclyzer", "subset", ["--print-cg", "--datalog-analysis=subset"]),
]

# ANSI color codes
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
RESET = '\033[0m'

def parse_dynamic(output):
    """Parse dynaa-check output (skip [log] lines) into {caller: {callee}}."""
    cg = {}
    for line in output.strip().split('\n'):
        line = line.strip()
        if line.startswith('[log]'):
            continue
        # Format: caller,,callee (callsite is empty, ignored)
        parts = line.split(',,')
        if len(parts) < 2:
            continue
        caller = parts[0].strip()
        callee = parts[-1].strip()
        if callee == "main":
            continue
        cg.setdefault(caller, set()).add(callee)
    return cg
def parse_aser(dot_file_path):
    """
    Parse a DOT call graph file and return {caller: {callee}} dictionary.
    Function names are extracted: find '@', then truncate at '('.
    """
    cg = {}
    
    # Fixed pattern: match label with @funcname( on a single line
    # The label is like: "{\<Empty\>\ni32 @main()}" or "{\<Empty\>\nindirect}"
    node_pattern = re.compile(r'Node0x([0-9a-f]+)\s*\[.*label="\{[^}]*@([^(]+)\(.*"\s*\]')
    
    edge_pattern = re.compile(r'Node0x([0-9a-f]+)\s*->\s*Node0x([0-9a-f]+);')
    
    # First pass: map node IDs to function names
    node_to_func = {}
    with open(dot_file_path, 'r') as f:
        for line in f:
            match = node_pattern.search(line)
            if match:
                node_id = match.group(1)
                func_name = match.group(2)
                node_to_func[node_id] = func_name
    
    # Second pass: build call graph from edges
    with open(dot_file_path, 'r') as f:
        for line in f:
            match = edge_pattern.search(line)
            if match:
                caller_id = match.group(1)
                callee_id = match.group(2)
                
                caller = node_to_func.get(caller_id)
                callee = node_to_func.get(callee_id)
                
                if caller and callee:
                    cg.setdefault(caller, set()).add(callee)
    
    return cg

def parse_SVF(filepath):
    result = {}

    node_pattern = re.compile(r'Node0x[0-9a-f]+\s*\[.*label="\{[^}]*\\{fun:\s*([^}]+)\\}')
    edge_pattern = re.compile(r'Node0x[0-9a-f]+:s\d+\s*->\s*Node0x[0-9a-f]+')

    id_to_func = {}
    with open(filepath, 'r') as f:
        for line in f:
            m = node_pattern.search(line)
            if m:

                id_match = re.match(r'Node0x([0-9a-f]+)', line.strip())
                if id_match:
                    node_id = '0x' + id_match.group(1)
                    func_name = m.group(1).strip()
                    id_to_func[node_id] = func_name

    with open(filepath, 'r') as f:
        for line in f:
            m = edge_pattern.search(line)
            if m:
                parts = line.strip().split('->')
                caller_part = parts[0].strip()
                callee_part = parts[1].strip()

                caller_id = '0x' + re.match(r'Node0x([0-9a-f]+)', caller_part).group(1)
                callee_id = '0x' + re.match(r'Node0x([0-9a-f]+)', callee_part).group(1)
                caller_func = id_to_func.get(caller_id)
                callee_func = id_to_func.get(callee_id)
                if caller_func and callee_func:
                    if caller_func not in result:
                        result[caller_func] = set()
                    result[caller_func].add(callee_func)
    return result

def parse_phasar(filepath):
    """
    Parse a Phasar DOT call graph file and return {caller: {callee}} dictionary.
    Format example:
      digraph CallGraph{
      0[label="callThroughNestedPtr"];
      1[label="add"];
      ...
      4->0[label="call void @callThroughNestedPtr(...) | ID: 117"];
      4->9[label="..."];
      }
    """
    cg = {}
    node_pattern = re.compile(r'^\s*(\d+)\[label="([^"]+)"\];')
    edge_pattern = re.compile(r'^\s*(\d+)->(\d+)\[')

    id_to_func = {}
    with open(filepath, 'r') as f:
        for line in f:
            m = node_pattern.match(line)
            if m:
                node_id = m.group(1)
                func_name = m.group(2)
                id_to_func[node_id] = func_name
        

    with open(filepath, 'r') as f:
        for line in f:
            m = edge_pattern.match(line)
            if m:
                caller_id = m.group(1)
                callee_id = m.group(2)
                caller = id_to_func.get(caller_id)
                callee = id_to_func.get(callee_id)
                if caller and callee:
                    cg.setdefault(caller, set()).add(callee)

    return cg

def parse_cclyzer(output):
    """
    Parse Cclyzer stdout output into {caller: {callee}} dictionary.
    Format: "caller -> callee" lines after "Call graph (N edges):"
    """
    cg = {}
    in_cg = False

    for line in output.strip().split('\n'):
        line = line.strip()

        # Detect start of call graph section
        if line.startswith('Call graph'):
            in_cg = True
            continue

        if in_cg:
            # Parse "caller -> callee" format
            parts = line.split(' -> ')
            if len(parts) == 2:
                caller = parts[0].strip()
                callee = parts[1].strip()
                if callee == "main":
                    continue
                cg.setdefault(caller, set()).add(callee)

    return cg

def run_command(cmd: str | list[str], capture=True):
    print(cmd if isinstance(cmd, str) else " ".join(cmd))
    import subprocess
    """Run a shell command; return combined stdout+stderr on success, exit on failure."""
    if isinstance(cmd, list):
        result = subprocess.run(cmd, capture_output=capture, text=True)
    else:
        result = subprocess.run(cmd, shell=True, capture_output=capture, text=True)
    if result.returncode != 0:
        print(f"{RED}Error: {result.stderr}{RESET}")
        sys.exit(1)
    # Return both stdout and stderr concatenated
    return result.stdout + result.stderr

def compare_callgraphs(dyn_cg, static_cg):
    """Return list of (caller, callee) edges present in dynamic but missing from static."""
    missing = []
    for caller, callees in dyn_cg.items():
        for callee in callees:
            if caller not in static_cg or callee not in static_cg[caller]:
                missing.append((caller, callee))
    return missing

def main():
    # If sys.argv provides file(s), use them; otherwise fall back to TESTS_PATH glob
    if len(sys.argv) > 1:
        files = []
        for arg in sys.argv[1:]:
            p = Path(arg)
            if p.exists():
                files.append(p)
            else:
                print(f"Warning: {arg} does not exist, skipping.")
        if not files:
            print("No valid files provided. Exiting.")
            return
    else:
        if not Path(TESTS_PATH).exists():
            print(f"Directory {TESTS_PATH} does not exist.")
            return
        files = sorted(Path(TESTS_PATH).glob('*.*'))

    results = {}

    for i, src_file in enumerate(files):
        file_name = src_file.name
        print(f"\nAnalyzing: {file_name}")

        try:
            # 1. Compile to LLVM IR and bitcode
            args = "-flto -fwhole-program-vtables -emit-llvm -c"
            compile_cmd=f"{clang} {str(src_file)} {args}"
            print(f"[*] Compiling to LLVM IR: {args}")
            run_command(f"{compile_cmd} -S -o {ll_file}")
            run_command(f"{compile_cmd} -o {bc_file}")

            # 2. Obtain dynamic call graph via dynaa-check
            print(f"[*] Instrument, link and run for dynamic call graph...")
            output = run_command(f"{python_executable} {dyncg} {bc_file}")
            # print(output)
            dyn_cg = parse_dynamic(output)
            total_edges = sum(len(v) for v in dyn_cg.values())
            print(f"[*] Dynamic call graph: {len(dyn_cg)} callers, {total_edges} edges.")

            # 3. Run static analyses and compare
            missing_results=[]
            for tool, label, args in configs:
                print(f"\n{YELLOW}[Static: {tool} {label}] {args}{RESET}")
                static_cg = None
                if tool == "SVF":
                    run_command(f"cd {SVF_PATH} && {wpa_path} {args} {ll_file}")
                    dot_file = os.path.join(SVF_PATH, 'callgraph_final.dot')
                    if not os.path.exists(dot_file):
                        print(f"  {RED}[-] callgraph_final.dot not found, skipping.{RESET}")
                        continue
                    static_cg = parse_SVF(dot_file)
                elif tool == "AserPTA":
                    output_dir = os.path.dirname(os.path.abspath(ll_file))
                    output_path = os.path.join(output_dir, "callgraph.dot")
                    run_command([aser_aa,"--callgraph",f"--finalcallgraphoutput={os.path.join(output_dir, 'callgraph')}",ll_file])
                    dot_file = os.path.join(os.path.dirname(ll_file), "callgraph.dot")
                    if not os.path.exists(dot_file):
                        print(f"  {RED}[-] callgraph.dot not found, skipping.{RESET}")
                        continue
                    static_cg = parse_aser(dot_file)
                elif tool == "Phasar":
                    output_dir = os.path.join(os.path.dirname(os.path.abspath(ll_file)),f"phasar_{i}")
                    os.makedirs(output_dir, exist_ok=True)
                    cmd = [phasar_cli_path, "--module", ll_file] + args + ["--out", output_dir]
                    run_command(cmd)
                    # phasar writes the dot file to the output directory with a timestamp subdirectory
                    import glob
                    dot_files = glob.glob(os.path.join(output_dir, "*", "*.txt"))
                    if not dot_files:
                        # try also the output_dir itself
                        dot_files = glob.glob(os.path.join(output_dir, "*.txt"))
                    if not dot_files:
                        print(f"  {RED}[-] phasar dot file not found, skipping.{RESET}")
                        # Clean up the empty directory
                        shutil.rmtree(output_dir, ignore_errors=True)
                        continue
                    # use the most recent dot file
                    dot_file = max(dot_files, key=os.path.getmtime)
                    print(dot_file)
                    print(f"  Found dot file: {dot_file}")
                    static_cg = parse_phasar(dot_file)
                    # Remove temporary phasar output directory
                    shutil.rmtree(output_dir, ignore_errors=True)
                elif tool == "Cclyzer":
                    # Cclyzer outputs to stdout
                    output = run_command([cclyzer_path, ll_file] + args)
                    static_cg = parse_cclyzer(output)

                    if not static_cg:
                        print(f"  {RED}[-] No call graph found in output, skipping.{RESET}")
                        continue

                missing = compare_callgraphs(dyn_cg, static_cg)
                missing_results.append(missing)

                if missing:
                    print(f"  {RED}[!] {len(missing)} edge(s) present dynamically but missing statically:{RESET}")
                    for caller, callee in missing:
                        print(f"    {RED}{caller} -> {callee}{RESET}")
                else:
                    print(f"  {GREEN}[+] All dynamic edges are covered statically.{RESET}")
            
            config_summary = []
            
            for (tool, label, _), missing in zip(configs, missing_results):
                config_name = f"{tool} {label}"
                missing_edges = [(caller, callee) for caller, callee in missing]
                config_summary.append((config_name, missing_edges))
            
            results[file_name] = {
                'total_edges': total_edges,
                'config_summary': config_summary
            }

        except Exception as e:
            print(f"  ERROR: {e}")
            results[file_name] = {'error': str(e)}

    # ========== Summary ==========
    print("\n\n" + "=" * 70)
    print("SUMMARY REPORT")
    print("=" * 70)

    for file_name, data in sorted(results.items()):
        if 'error' in data:
            print(f"\n{file_name}: ERROR - {data['error']}")
            continue
            
        print(f"\n{file_name}:")
        print(f"  Total dynamic edges: {data['total_edges']}")
        
        for config_name, missing_edges in data['config_summary']:
            print(f"  [{config_name}] Missing edges: {len(missing_edges)}")
            if missing_edges:
                for caller, callee in missing_edges[:10]:
                    print(f"    {caller} -> {callee}")
                if len(missing_edges) > 10:
                    print(f"    ...")
            else:
                print("    All dynamic edges covered.")
if __name__ == "__main__":
    main()