#!/bin/bash

EXECUTABLE="../cmake-build-debug/basic_resolve"

if [ ! -f "$EXECUTABLE" ]; then
    echo "[!] Error: Executable '$EXECUTABLE' not found!"
    exit 1
fi

COUNT=0

while true; do
    ((COUNT++))
    echo "=========================================="
    echo "--- Run #$COUNT ---"
    echo "=========================================="

    # Run the binary
    $EXECUTABLE
    EXIT_CODE=$?

    # Print the return code for visibility
    echo "--> Process finished with exit code: $EXIT_CODE"

    # If the process crashed or returned a non-zero exit code
    if [ $EXIT_CODE -ne 0 ]; then
        echo ""
        echo "[!] CRASH / ERROR DETECTED on run #$COUNT!"
        echo "[!] Exit Code: $EXIT_CODE"

        if [ $EXIT_CODE -eq 139 ]; then
            echo "[!] Cause: Segmentation Fault (SIGSEGV)"
        elif [ $EXIT_CODE -eq 134 ]; then
            echo "[!] Cause: Aborted / Assertion Failed (SIGABRT)"
        elif [ $EXIT_CODE -eq 137 ]; then
            echo "[!] Cause: Out of Memory / Killed (SIGKILL)"
        fi

        echo ""
        read -p "Press Enter to exit..."
        break
    fi

    # Small sleep optional to avoid CPU thrashing between runs
    # sleep 0.1
done