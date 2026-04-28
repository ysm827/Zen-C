#!/bin/bash

# Zen-C Benchmarking Script
# Outputs JSON format compatible with github-action-benchmark

ZC="./zc"
if [ ! -f "$ZC" ]; then
    ZC="./build/zc"
fi

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found."
    exit 1
fi

RESULTS="[]"

add_result() {
    local name="$1"
    local value="$2"
    local unit="$3"
    local entry="{\"name\": \"$name\", \"value\": $value, \"unit\": \"$unit\"}"
    if [ "$RESULTS" == "[]" ]; then
        RESULTS="[$entry]"
    else
        RESULTS="${RESULTS%]}, $entry]"
    fi
}

echo "--- Benchmarking Compiler Performance ---"
# Measure time to transpile all tests (stress test for frontend + codegen)
FILE_COUNT=$(find tests -name "*.zc" -not -name "_*.zc" | wc -l)
START=$(date +%s%3N)
find tests -name "*.zc" -not -name "_*.zc" | xargs -L 1 $ZC transpile -o .bench_temp.c -w > /dev/null 2>&1
END=$(date +%s%3N)
rm -f .bench_temp.c
COMP_TIME=$((END - START))
AVG_TIME=$((COMP_TIME / FILE_COUNT))
echo "Compiler (Full Suite Transpilation): $COMP_TIME ms ($FILE_COUNT files, avg $AVG_TIME ms/file)"
add_result "Compiler (Full Suite Transpilation)" "$COMP_TIME" "ms"
add_result "Compiler (Avg per file)" "$AVG_TIME" "ms"

echo "--- Benchmarking Runtime Performance ---"

for bench in tests/bench/*.zc; do
    name=$(basename "$bench" .zc)
    echo "Running $name..."
    
    # Compile benchmark
    $ZC build "$bench" -o "bench_$name" -w
    if [ $? -ne 0 ]; then
        echo "Failed to compile $bench"
        continue
    fi
    
    # Run benchmark and capture RESULT
    output=$(./bench_$name)
    result=$(echo "$output" | grep "RESULT:" | cut -d' ' -f2)
    
    if [ -n "$result" ]; then
        echo "$name: $result ms"
        add_result "Runtime ($name)" "$result" "ms"
    else
        echo "Failed to get result for $name"
    fi
    
    rm "bench_$name"
done

echo "$RESULTS" > benchmarks_result.json
echo "Results saved to benchmarks_result.json"
