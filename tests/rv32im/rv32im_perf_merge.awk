function field(name, i, kv) {
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    if (kv[1] == name)
      return kv[2]
  }
  return ""
}

function keep_field(value, kv, name) {
  split(value, kv, "=")
  name = kv[1]
  return name != "rdinstret_csr_raw" &&
    name != "rdinstret_csr_overhead" &&
    name != "rdinstret_csr_net" &&
    name != "rdinstret_csr_verified" &&
    name != "counter_source" &&
    name != "counter_semantics"
}

BEGIN {
  if (!expected_trace_windows)
    expected_trace_windows = 14
  if (!expected_reported_measurements)
    expected_reported_measurements = 13
}

NR == FNR {
  if ($1 ~ /^window=/ && $2 ~ /^qemu_trace_raw=/) {
    split($1, a, "=")
    split($2, b, "=")
    trace_raw[a[2] + 0] = b[2] + 0
    trace_windows++
  }
  next
}

/ command=rv32im-perf / {
  workload = field("workload")
  phase = field("phase")
  measured = (workload == "control" && phase == "probe") ||
    (workload != "all" && (phase == "cold" || phase == "warm"))

  if (measured) {
    measurement_index++
    window = measurement_index + 1
    if (!(window in trace_raw)) {
      print "missing qemu trace count for measurement " measurement_index > "/dev/stderr"
      failed = 1
      next
    }

    if (measurement_index == 1) {
      print "result=PASS command=rv32im-perf workload=empty phase=control backend=rv32im guest_insns=0 guest_cycles=0 executed_rv32_insns_raw=" trace_raw[1] " executed_rv32_insns_overhead=" trace_raw[1] " executed_rv32_insns=0 executed_rv32_insns_per_guest_x100=0 harness_mode=runtime_fixture counter_source=qemu_exec_trace counter_semantics=qemu_tb_instruction_sum rdinstret_csr_verified=0 reason=measurement_overhead"
    }

    csr_raw = field("rdinstret_csr_raw")
    csr_overhead = field("rdinstret_csr_overhead")
    csr_net = field("rdinstret_csr_net")
    guest_insns = field("guest_insns") + 0
    net = trace_raw[window] - trace_raw[1]
    if (net < 0) {
      print "negative qemu trace net count for " workload ":" phase > "/dev/stderr"
      failed = 1
      next
    }

    output = ""
    for (i = 1; i <= NF; i++) {
      if (keep_field($i))
        output = output (output == "" ? "" : " ") $i
    }
    per_guest = guest_insns ? int((net * 100) / guest_insns) : 0
    output = output " executed_rv32_insns_raw=" trace_raw[window]
    output = output " executed_rv32_insns_overhead=" trace_raw[1]
    output = output " executed_rv32_insns=" net
    output = output " executed_rv32_insns_per_guest_x100=" per_guest
    output = output " rdinstret_csr_raw=" csr_raw
    output = output " rdinstret_csr_overhead=" csr_overhead
    output = output " rdinstret_csr_net=" csr_net
    output = output " counter_source=qemu_exec_trace"
    output = output " counter_semantics=qemu_tb_instruction_sum"
    output = output " rdinstret_csr_verified=0"
    print output
    next
  }

  print
  next
}

{
  print
}

END {
  if (trace_windows != expected_trace_windows) {
    print "expected " expected_trace_windows " qemu trace counts, got " \
      trace_windows > "/dev/stderr"
    failed = 1
  }
  if (measurement_index != expected_reported_measurements) {
    print "expected " expected_reported_measurements \
      " reported measurements, got " measurement_index > "/dev/stderr"
    failed = 1
  }
  if (failed)
    exit 1
}
