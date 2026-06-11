#!/usr/bin/env python3
"""Benchmark regression checker for coco coroutine library.

Usage:
    ./tools/bench_regress.py [baseline.json]

If baseline.json is provided, compares current results against baseline.
Otherwise, just runs benchmarks and saves results to benchmark_results.json.
"""

import subprocess
import json
import sys
import os
import time

BENCHMARKS = [
    'bench_switch',
    'bench_channel',
    'bench_io',
    'bench_mt_sched',
    'bench_preempt',
    'bench_hot_stack',
    'bench_hot_cold_switch',
    'bench_stack',
]

def run_benchmark(name, build_dir='build/tests/benchmark'):
    """Run a single benchmark and return parsed results."""
    bench_path = os.path.join(build_dir, name)
    if not os.path.exists(bench_path):
        print(f"SKIP: {name} not found at {bench_path}")
        return None
    
    try:
        result = subprocess.run(
            [bench_path],
            capture_output=True,
            text=True,
            timeout=60
        )
        
        return {
            'name': name,
            'returncode': result.returncode,
            'stdout': result.stdout,
            'stderr': result.stderr,
            'status': 'PASS' if result.returncode == 0 else 'FAIL'
        }
    except subprocess.TimeoutExpired:
        return {'name': name, 'status': 'TIMEOUT'}
    except Exception as e:
        return {'name': name, 'status': f'ERROR: {e}'}

def extract_metric(result, keyword='ns/op'):
    """Extract numeric metric from benchmark output."""
    if not result or result['status'] != 'PASS':
        return None
    
    for line in result['stdout'].split('\n'):
        if keyword in line:
            # Try to find a number
            parts = line.split()
            for part in parts:
                try:
                    return float(part)
                except ValueError:
                    continue
    return None

def main():
    build_dir = 'build'
    baseline_path = sys.argv[1] if len(sys.argv) > 1 else None
    
    # Load baseline if provided
    baseline = {}
    if baseline_path and os.path.exists(baseline_path):
        with open(baseline_path) as f:
            baseline = json.load(f)
    
    results = {}
    all_pass = True
    
    print("Running benchmark suite...")
    print("=" * 60)
    
    for bench in BENCHMARKS:
        print(f"\nRunning {bench}...")
        result = run_benchmark(bench, build_dir)
        
        if result is None:
            continue
        
        results[bench] = result
        status = result['status']
        
        if status == 'PASS':
            # Show key metrics from output
            for line in result['stdout'].split('\n')[:5]:
                if line.strip():
                    print(f"  {line}")
            
            # Compare with baseline if available
            if bench in baseline and baseline[bench]['status'] == 'PASS':
                old_stdout = baseline[bench].get('stdout', '')
                # Simple string comparison for now
                if result['stdout'] != old_stdout:
                    print(f"  NOTE: Output differs from baseline")
        else:
            print(f"  FAILED: {status}")
            if result.get('stderr'):
                print(f"  stderr: {result['stderr'][:200]}")
            all_pass = False
    
    print("\n" + "=" * 60)
    
    # Save results
    output_path = 'build/benchmark_results.json'
    with open(output_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"Results saved to {output_path}")
    
    if baseline_path:
        print(f"Compared against baseline: {baseline_path}")
    
    if all_pass:
        print("All benchmarks PASSED")
        return 0
    else:
        print("Some benchmarks FAILED")
        return 1

if __name__ == '__main__':
    sys.exit(main())
