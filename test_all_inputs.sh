#!/bin/bash

echo "================================================================"
echo "          COMPREHENSIVE TEST - ALL INPUT FILES"
echo "================================================================"
echo ""

# List of all test files (excluding Zone.Identifier files)
tests=(
    "sched"
    "sched_0"
    "sched_1"
    "os_sc"
    "os_syscall"
    "os_syscall_list"
    "os_0_mlq_paging"
    "os_1_mlq_paging"
    "os_1_mlq_paging_small_1K"
    "os_1_mlq_paging_small_4K"
    "os_1_singleCPU_mlq"
    "os_1_singleCPU_mlq_paging"
)

passed=0
failed=0
timeout_count=0

for testfile in "${tests[@]}"; do
    echo "-----------------------------------------------------------"
    echo "TEST: $testfile"
    echo -n "Running... "
    
    # Run with 10 second timeout
    if timeout 10 ./os "$testfile" > "test_results/${testfile}.out" 2>&1; then
        lines=$(wc -l < "test_results/${testfile}.out")
        
        # Check if output has meaningful content
        if [ $lines -gt 5 ]; then
            echo " PASSED ($lines lines)"
            
            # Analyze output
            has_cpu=$(grep -c "CPU" "test_results/${testfile}.out" 2>/dev/null || echo 0)
            has_load=$(grep -c "Loaded" "test_results/${testfile}.out" 2>/dev/null || echo 0)
            has_dispatch=$(grep -c "Dispatched" "test_results/${testfile}.out" 2>/dev/null || echo 0)
            has_syscall=$(grep -c "sys_" "test_results/${testfile}.out" 2>/dev/null || echo 0)
            has_finish=$(grep -c "finished" "test_results/${testfile}.out" 2>/dev/null || echo 0)
            
            echo "  └─ CPU ops: $has_cpu, Loaded: $has_load, Dispatched: $has_dispatch, Syscalls: $has_syscall, Finished: $has_finish"
            
            # Compare with expected output if exists
            if [ -f "output/${testfile}.output" ]; then
                expected_lines=$(wc -l < "output/${testfile}.output")
                echo "  └─ Expected: $expected_lines lines (actual: $lines)"
            fi
            
            passed=$((passed + 1))
        else
            echo " MINIMAL OUTPUT ($lines lines)"
            cat "test_results/${testfile}.out"
            failed=$((failed + 1))
        fi
    else
        exit_code=$?
        if [ $exit_code -eq 124 ]; then
            echo " TIMEOUT (>10 seconds)"
            timeout_count=$((timeout_count + 1))
        else
            echo " FAILED (exit code: $exit_code)"
            failed=$((failed + 1))
        fi
    fi
    echo ""
done

echo "================================================================"
echo "                      SUMMARY"
echo "================================================================"
echo "✓ Passed:  $passed"
echo "✗ Failed:  $failed"
echo "⏱ Timeout: $timeout_count"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
total=$((passed + failed + timeout_count))
echo "Total:     $total tests"

if [ $failed -eq 0 ] && [ $timeout_count -eq 0 ]; then
    echo ""
    echo " ALL TESTS PASSED! "
    exit 0
else
    echo ""
    echo " Some tests need attention"
    exit 1
fi
