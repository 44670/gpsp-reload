function field(name, i, pair) {
  for (i = 1; i <= NF; i++) {
    split($i, pair, "=")
    if (pair[1] == name)
      return pair[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im control-flow check failed: " reason > "/dev/stderr"
  failed = 1
}

function add_workload(name, warm_iterations, direct_per_run,
                      dispatcher_per_run, indirect_hits_per_run) {
  workloads[++workload_count] = name
  warm_runs[name] = warm_iterations
  direct_runs[name] = direct_per_run
  dispatcher_runs[name] = dispatcher_per_run
  indirect_runs[name] = indirect_hits_per_run
}

BEGIN {
  add_workload("mapped_alu", 128, 0, 1, 0)
  add_workload("memory_read", 64, 0, 1, 0)
  add_workload("branch_chain", 128, 31, 1, 0)
  add_workload("indirect_lookup", 128, 0, 16, 15)
  add_workload("scheduler", 2048, 0, 1, 0)
  add_workload("mixed", 64, 1, 1, 0)
}

/ command=rv32im-perf / && / workload=all / && / phase=summary / {
  summary_seen++
  if ((field("workloads") + 0) != 6 ||
      (field("cold") + 0) != 6 || (field("warm") + 0) != 6)
    fail("suite summary workload count changed")
}

/ command=rv32im-perf / {
  workload = field("workload")
  phase = field("phase")
  key = workload ":" phase

  if (!(workload in warm_runs) || (phase != "cold" && phase != "warm"))
    next

  seen[key]++
  iterations = phase == "cold" ? 1 : warm_runs[workload]
  expected_dispatcher = dispatcher_runs[workload] * iterations
  expected_direct = direct_runs[workload] * iterations
  expected_indirect = indirect_runs[workload] * iterations
  expected_lookup_stubs = expected_indirect
  expected_slow_paths = iterations

  if (field("result") != "PASS")
    fail(key " was not PASS")
  if ((field("iterations") + 0) != iterations)
    fail(key " iteration count changed")
  if (field("control_counter_source") != "runtime_instrumented" ||
      field("control_counter_semantics") != "taken_runtime_events")
    fail(key " did not use taken-event runtime counters")
  if ((field("dispatcher_entries") + 0) != expected_dispatcher)
    fail(key " dispatcher entry count changed")
  if ((field("lookup_stub_entries") + 0) != expected_lookup_stubs ||
      (field("slow_path_entries") + 0) != expected_slow_paths ||
      expected_lookup_stubs + expected_slow_paths != expected_dispatcher)
    fail(key " lookup/slow stub split changed")
  if ((field("direct_chain_attempts") + 0) != expected_direct ||
      (field("direct_chain_hits") + 0) != expected_direct)
    fail(key " direct-chain taken counts changed")
  if ((field("cycle_exits") + 0) != iterations)
    fail(key " cycle-exit count changed")
  if ((field("indirect_lookup_hits") + 0) != expected_indirect ||
      (field("indirect_lookup_misses") + 0) != 0)
    fail(key " indirect lookup counts changed")
  if ((field("fallthrough_lookup_hits") + 0) != 0 ||
      (field("fallthrough_lookup_misses") + 0) != 0)
    fail(key " unexpectedly took a fallthrough lookup")
  if ((field("scheduler_updates") + 0) != iterations ||
      (field("scheduler_exits") + 0) != iterations)
    fail(key " scheduler count changed")
  if ((field("fallbacks") + 0) != 0 ||
      (field("execute_arm_calls") + 0) != 0)
    fail(key " used an interpreter fallback")
}

/ command=rv32im-control-flow / && / case=indirect_miss / {
  miss_seen++
  if (field("result") != "PASS" ||
      field("control_counter_source") != "runtime_instrumented" ||
      (field("dispatcher_entries") + 0) != 1 ||
      (field("direct_chain_attempts") + 0) != 0 ||
      (field("direct_chain_hits") + 0) != 0 ||
      (field("cycle_exits") + 0) != 0 ||
      (field("indirect_lookup_hits") + 0) != 0 ||
      (field("indirect_lookup_misses") + 0) != 1 ||
      (field("fallthrough_lookup_hits") + 0) != 0 ||
      (field("fallthrough_lookup_misses") + 0) != 0 ||
      (field("scheduler_updates") + 0) != 0 ||
      (field("lookup_stub_entries") + 0) != 1 ||
      (field("slow_path_entries") + 0) != 1 ||
      (field("lookups") + 0) != 2 ||
      (field("terminal_calls") + 0) != 1 ||
      (field("fallbacks") + 0) != 1 ||
      (field("relookup_fallbacks") + 0) != 1 ||
      (field("execute_arm_calls") + 0) != 1 ||
      field("reason") != "runtime_miss_counted")
    fail("indirect miss path did not report the exact taken events")
}

/ command=rv32im-control-flow / && / case=fallthrough_lookup / {
  fallthrough_seen++
  if (field("result") != "PASS" ||
      (field("dispatcher_entries") + 0) != 2 ||
      (field("lookup_stub_entries") + 0) != 1 ||
      (field("slow_path_entries") + 0) != 1 ||
      (field("fallthrough_lookup_hits") + 0) != 1 ||
      (field("scheduler_updates") + 0) != 1 ||
      (field("cycle_exits") + 0) != 1 ||
      (field("lookups") + 0) != 2 ||
      (field("terminal_calls") + 0) != 2 ||
      (field("update_calls") + 0) != 1 ||
      field("update_cycles") != "0x00000000" ||
      field("pc") != "0x08017008" ||
      field("reason") != "lookup_bypassed_scheduler_slow_path")
    fail("fallthrough lookup did not preserve resident cycles and split stubs")
}

END {
  for (wi = 1; wi <= workload_count; wi++) {
    name = workloads[wi]
    if (seen[name ":cold"] != 1)
      fail("missing exactly one " name ":cold result")
    if (seen[name ":warm"] != 1)
      fail("missing exactly one " name ":warm result")
  }
  if (miss_seen != 1)
    fail("missing exactly one indirect miss result")
  if (fallthrough_seen != 1)
    fail("missing exactly one fallthrough lookup result")
  if (summary_seen != 1)
    fail("missing exactly one suite summary")
  if (failed)
    exit 1
  print "result=PASS command=rv32im-control-flow case=all workloads=6 " \
    "runtime_taken_events=1 split_control_stubs=1 " \
    "indirect_hit_and_miss=1 repeatable=1 " \
    "reason=control_flow_counters_verified"
}
