function field(name, i, kv) {
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    if (kv[1] == name)
      return kv[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im perf A/B check failed: " reason > "/dev/stderr"
  failed = 1
}

function capture(prefix, key) {
  seen[prefix ":" key]++
  result[prefix ":" key] = field("result")
  guest[prefix ":" key] = field("guest_insns")
  cycles[prefix ":" key] = field("guest_cycles")
  insns[prefix ":" key] = field("executed_rv32_insns")
  raw_insns[prefix ":" key] = field("executed_rv32_insns_raw")
  overhead_insns[prefix ":" key] = field("executed_rv32_insns_overhead")
  bytes[prefix ":" key] = field("generated_bytes")
  state[prefix ":" key] = field("state_hash")
  memory[prefix ":" key] = field("memory_hash")
  scheduler[prefix ":" key] = field("scheduler_hash")
  trace[prefix ":" key] = field("trace_hash")
  optimized_trace[prefix ":" key] = field("optimized_trace_hash")
  source[prefix ":" key] = field("counter_source")
  semantics[prefix ":" key] = field("counter_semantics")
  csr_verified[prefix ":" key] = field("rdinstret_csr_verified")
  scope[prefix ":" key] = field("measurement_scope")
  fallbacks[prefix ":" key] = field("fallbacks")
  interp_calls[prefix ":" key] = field("execute_arm_calls")
  retired[prefix ":" key] = field("retired_insns")
  legacy_instret[prefix ":" key] = field("rv32_instret")
}

function check_observation(prefix, key, mode, expected_trace, expected_scope,
                           adjusted) {
  if (result[prefix ":" key] != "PASS")
    fail(prefix ":" key " had a non-PASS profile")
  if (source[prefix ":" key] != "qemu_exec_trace" ||
      semantics[prefix ":" key] != "qemu_tb_instruction_sum")
    fail(prefix ":" key " counter semantics changed")
  expected_scope = (mode == "cold" ? "translate_sync_first_execute" : "steady_state_execute")
  if (scope[prefix ":" key] != expected_scope)
    fail(prefix ":" key " measurement scope changed")
  adjusted = (raw_insns[prefix ":" key] + 0) - (overhead_insns[prefix ":" key] + 0)
  if (adjusted != (insns[prefix ":" key] + 0))
    fail(prefix ":" key " executed-instruction adjustment changed")
  if (csr_verified[prefix ":" key] != "0" ||
      retired[prefix ":" key] != "" ||
      legacy_instret[prefix ":" key] != "")
    fail(prefix ":" key " mislabeled diagnostic rdinstret")
  if ((fallbacks[prefix ":" key] + 0) != 0 ||
      (interp_calls[prefix ":" key] + 0) != 0)
    fail(prefix ":" key " used interpreter fallback")
  if (guest[prefix ":" key] != guest["spec:" key] ||
      cycles[prefix ":" key] != cycles["spec:" key] ||
      state[prefix ":" key] != state["spec:" key] ||
      memory[prefix ":" key] != memory["spec:" key] ||
      scheduler[prefix ":" key] != scheduler["spec:" key])
    fail(prefix ":" key " changed frozen workload/result contract")
  expected_trace = trace["spec:" key]
  if (prefix == "optimized" && optimized_trace["spec:" key] != "")
    expected_trace = optimized_trace["spec:" key]
  if (trace[prefix ":" key] != expected_trace)
    fail(prefix ":" key " changed frozen lookup-trace contract")
}

BEGIN {
  workload[1] = "mapped_alu"
  workload[2] = "memory_read"
  workload[3] = "branch_chain"
  workload[4] = "indirect_lookup"
  workload[5] = "scheduler"
  workload[6] = "mixed"
  phase[1] = "cold"
  phase[2] = "warm"
}

FILENAME == spec_file && /^workload=/ {
  name = field("workload")
  mode = field("phase")
  if (mode == "cold" || mode == "warm")
    capture("spec", name ":" mode)
  next
}

FILENAME != spec_file && / command=rv32im-perf / {
  name = field("workload")
  mode = field("phase")
  if (FILENAME == baseline_file)
    prefix = "baseline"
  else if (FILENAME == optimized_file)
    prefix = "optimized"
  else
    prefix = "unknown"

  if (name == "all" && mode == "summary") {
    summary[prefix]++
    summary_profile[prefix] = field("profile")
    summary_workloads[prefix] = field("workloads")
    summary_cold[prefix] = field("cold")
    summary_warm[prefix] = field("warm")
    next
  }
  if (mode == "cold" || mode == "warm")
    capture(prefix, name ":" mode)
  next
}

FILENAME != spec_file && / command=rv32im-perf-env / {
  if (FILENAME == baseline_file)
    prefix = "baseline"
  else if (FILENAME == optimized_file)
    prefix = "optimized"
  else
    prefix = "unknown"
  if (field("result") != "PASS")
    fail(prefix " environment was not locked")
  env_seen[prefix]++
}

END {
  for (wi = 1; wi <= 6; wi++) {
    for (pi = 1; pi <= 2; pi++) {
      name = workload[wi]
      mode = phase[pi]
      key = name ":" mode

      if (seen["spec:" key] != 1 || seen["optimized:" key] != 1) {
        fail(key " missing spec/optimized observation")
        continue
      }
      check_observation("optimized", key, mode)

      if (name != "mapped_alu")
        continue
      if (seen["baseline:" key] != 1) {
        fail(key " missing focused baseline observation")
        continue
      }
      check_observation("baseline", key, mode)
      if (insns["baseline:" key] != insns["spec:" key] ||
          bytes["baseline:" key] != bytes["spec:" key])
        fail(key " no longer reproduces frozen baseline")
      if ((insns["optimized:" key] + 0) > (insns["baseline:" key] + 0))
        fail(key " regressed against its focused baseline")
      if ((bytes["optimized:" key] + 0) >= (bytes["baseline:" key] + 0))
        fail(key " generated bytes did not decrease")

      reduction = int((insns["baseline:" key] - \
                       insns["optimized:" key]) * 10000 / \
                       insns["baseline:" key])
      report[++report_count] = "result=PASS command=rv32im-perf-ab workload=" name \
        " phase=" mode \
        " baseline_executed_rv32_insns=" insns["baseline:" key] \
        " optimized_executed_rv32_insns=" insns["optimized:" key] \
        " executed_insns_reduction_percent_x100=" reduction \
        " baseline_generated_bytes=" bytes["baseline:" key] \
        " optimized_generated_bytes=" bytes["optimized:" key] \
        " counter_source=qemu_exec_trace counter_semantics=qemu_tb_instruction_sum" \
        " rdinstret_csr_verified=0 reason=focused_mapped_alu_baseline_equal"
    }
  }

  key = "mapped_alu:warm"
  if ((insns["optimized:" key] + 0) * 100 > \
      (insns["baseline:" key] + 0) * 75)
    fail("mapped_alu:warm improvement was below 25 percent")
  if (summary["baseline"] != 1 ||
      summary_profile["baseline"] != "mapped_alu_baseline" ||
      summary_workloads["baseline"] != "1" ||
      summary_cold["baseline"] != "1" || summary_warm["baseline"] != "1")
    fail("missing focused baseline profile summary")
  if (summary["optimized"] != 1 ||
      summary_profile["optimized"] != "optimized" ||
      summary_workloads["optimized"] != "6" ||
      summary_cold["optimized"] != "6" || summary_warm["optimized"] != "6")
    fail("missing optimized profile summary")
  if (env_seen["baseline"] != 1 || env_seen["optimized"] != 1)
    fail("missing locked environment evidence")
  if (failed)
    exit 1
  for (ri = 1; ri <= report_count; ri++)
    print report[ri]
  print "result=PASS command=rv32im-perf-ab workload=all profiles=2 " \
    "frozen_workloads=6 baseline_workloads=1 optimized_workloads=6 " \
    "correctness_hashes_equal=1 reason=focused_mapped_alu_ab_verified"
}
