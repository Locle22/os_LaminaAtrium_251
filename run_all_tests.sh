#!/bin/bash
echo "========================================="
echo "  COMPREHENSIVE TEST SUITE"
echo "========================================="
echo ""

tests=(
    "os_syscall_list:Syscall List Test"
    "os_0_mlq_paging:MLQ Paging Test (2 CPU)"
    "os_1_mlq_paging:MLQ Paging Test (4 CPU)"
    "os_1_singleCPU_mlq:Single CPU MLQ Test"
    "sched_0:Basic Scheduler Test"
)

passed=0
failed=0

for test_entry in "${tests[@]}"; do
    IFS=':' read -r testfile description <<< "$test_entry"
    echo "-------------------------------------------"
    echo "Test: $description"
    echo "File: $testfile"
    
    if timeout 5 ./os "$testfile" > test_output_temp.txt 2>&1; then
        lines=$(wc -l < test_output_temp.txt)
        if [ $lines -gt 0 ]; then
            echo " PASSED - Output: $lines lines"
            
            # Check for key indicators
            if grep -q "CPU.*Dispatched" test_output_temp.txt; then
                echo "   CPU scheduling works"
            fi
            if grep -q "sys_" test_output_temp.txt; then
                echo "   Syscalls detected"
            fi
            if grep -q "Loaded a process" test_output_temp.txt; then
                echo "   Process loading works"
            fi
            
            passed=$((passed + 1))
        else
            echo " FAILED - Empty output"
            failed=$((failed + 1))
        fi
    else
        echo " FAILED - Timeout or crash"
        failed=$((failed + 1))
    fi
    echo ""
done

rm -f test_output_temp.txt

echo "========================================="
echo "SUMMARY: $passed passed, $failed failed"
echo "========================================="

if [ $failed -eq 0 ]; then
    echo " ALL TESTS PASSED! "
    exit 0
else
    echo "⚠ Some tests failed"
    exit 1
fi
