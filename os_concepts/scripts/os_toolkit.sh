#!/bin/bash
#
# os_toolkit.sh - Menu driver for the OS concepts toolkit.
#
# Shell concepts: functions, case, while loops, input validation, pipes,
#                 tee (write to screen and file), PIPESTATUS, make.
# OS concepts:    Unit 1 - shell programming and system commands
#                 Unit 5 - file management (saving and reading log files)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN_DIR="$PROJECT_DIR/bin"
LOG_DIR="$PROJECT_DIR/logs"
mkdir -p "$LOG_DIR"

# Build all C programs with make if a binary is missing.
ensure_built() {
    if [ ! -x "$BIN_DIR/$1" ]; then
        echo "Building C programs with make..."
        if ! make -s -C "$PROJECT_DIR"; then
            echo "Build failed. Check the compiler errors above."
            return 1
        fi
    fi
}

# Run one C program, show output on screen and append it to its log file.
# $1 = binary name, $2 = title shown in the log
run_module() {
    local bin="$1" title="$2"
    local log="$LOG_DIR/$bin.log"

    ensure_built "$bin" || return 1
    echo "=== $title | $(date '+%Y-%m-%d %H:%M:%S') ===" | tee -a "$log"
    "$BIN_DIR/$bin" | tee -a "$log"
    local status=${PIPESTATUS[0]}
    echo "=== exit status: $status ===" | tee -a "$log"
    echo
}

# Read an integer between $2 and $3 (inclusive) with prompt $1.
# Result is stored in REPLY_INT.
read_int() {
    local value
    while true; do
        if ! read -rp "$1" value; then      # end of input (Ctrl+D): quit cleanly
            echo
            echo "Input closed. Goodbye."
            exit 0
        fi
        if [[ "$value" =~ ^[0-9]+$ ]] && [ "$value" -ge "$2" ] && [ "$value" -le "$3" ]; then
            REPLY_INT="$value"
            return 0
        fi
        echo "Please enter a number from $2 to $3."
    done
}

view_logs() {
    shopt -s nullglob
    local files=("$LOG_DIR"/*.log)
    shopt -u nullglob

    if [ ${#files[@]} -eq 0 ]; then
        echo "No logs yet. Run a module first."
        return
    fi

    echo "Saved logs:"
    for f in "${files[@]}"; do
        echo "  $(basename "$f")  ($(wc -l < "$f") lines)"
    done
    read -rp "Enter a log file name to view its last 40 lines (or press Enter): " name
    if [ -n "$name" ]; then
        if [ -f "$LOG_DIR/$name" ]; then
            tail -n 40 "$LOG_DIR/$name"
        else
            echo "No log named '$name'."
        fi
    fi
    echo
}

show_menu() {
    echo "=================================================="
    echo "           OS CONCEPTS TOOLKIT"
    echo "=================================================="
    echo "1. CPU scheduling: FCFS, SJF, Round Robin    (Unit 2)"
    echo "2. Process creation: fork, exec, wait        (Unit 1, 2)"
    echo "3. Producer-consumer with semaphores         (Unit 3)"
    echo "4. Deadlock avoidance and detection          (Unit 3)"
    echo "5. Page replacement: FIFO, LRU, Optimal      (Unit 4)"
    echo "6. Disk scheduling: FCFS, SSTF, SCAN         (Unit 5)"
    echo "7. View saved logs                           (Unit 5)"
    echo "8. Exit"
    echo "=================================================="
}

while true; do
    show_menu
    read_int "Enter choice (1-8): " 1 8
    case "$REPLY_INT" in
        1) run_module scheduler "CPU Scheduling" ;;
        2) run_module process_demo "Process Creation (fork/exec/wait)" ;;
        3) run_module prodcons "Producer-Consumer" ;;
        4) run_module deadlock "Deadlock Avoidance/Detection" ;;
        5) run_module paging "Page Replacement" ;;
        6) run_module disk "Disk Scheduling" ;;
        7) view_logs ;;
        8) echo "Goodbye."; exit 0 ;;
    esac
done
