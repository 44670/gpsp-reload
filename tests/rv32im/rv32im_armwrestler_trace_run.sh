#!/bin/sh
set -eu

if test "$#" -ne 8; then
  echo "usage: $0 QEMU LLVM_NM TRACE_AWK BINARY COUNTS_LOG STDOUT_LOG FIFO TIMEOUT_SECONDS" >&2
  exit 2
fi

qemu=$1
llvm_nm=$2
trace_awk=$3
binary=$4
counts_log=$5
stdout_log=$6
trace_fifo=$7
timeout_seconds=$8

case "$timeout_seconds" in
  ''|*[!0-9]*)
    echo "result=FAIL command=armwrestler-perf timeout_seconds=$timeout_seconds reason=invalid_timeout" >&2
    exit 2
    ;;
esac
if test "$timeout_seconds" -eq 0; then
  echo "result=FAIL command=armwrestler-perf timeout_seconds=0 reason=invalid_timeout" >&2
  exit 2
fi

cleanup()
{
  rm -f "$trace_fifo"
}
trap cleanup EXIT HUP INT TERM

cleanup
mkfifo "$trace_fifo"
begin_pc=$($llvm_nm -n "$binary" | awk '$3 == "rv32im_armwrestler_measure_begin" { print $1 }')
end_pc=$($llvm_nm -n "$binary" | awk '$3 == "rv32im_armwrestler_measure_end" { print $1 }')
if test -z "$begin_pc" || test -z "$end_pc"; then
  echo "result=FAIL command=armwrestler-perf reason=measurement_markers_missing" >&2
  exit 1
fi

awk -v begin_pc="$begin_pc" -v end_pc="$end_pc" -v expected_windows=16 \
  -f "$trace_awk" "$trace_fifo" > "$counts_log" &
counter_pid=$!

qemu_rc=0
timeout --signal=TERM --kill-after=5s "${timeout_seconds}s" \
  "$qemu" -d in_asm,exec,nochain -D "$trace_fifo" "$binary" \
  > "$stdout_log" || qemu_rc=$?
counter_rc=0
wait "$counter_pid" || counter_rc=$?

if test "$qemu_rc" -ne 0 || test "$counter_rc" -ne 0; then
  echo "result=FAIL command=armwrestler-perf qemu_rc=$qemu_rc counter_rc=$counter_rc reason=qemu_exec_trace_runner_failed" >&2
  exit 1
fi
