function field(name, i, pair) {
  for (i = 1; i <= NF; i++) {
    split($i, pair, "=")
    if (pair[1] == name)
      return pair[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im lookup control-flow check failed: " reason > "/dev/stderr"
  failed = 1
}

function add_workload(name, warm_iterations, direct_per_run,
                      dispatcher_cold_per_run, dispatcher_warm_per_run,
                      slow_indirect_cold_per_run, cache_per_run) {
  workloads[++workload_count] = name
  warm_runs[name] = warm_iterations
  direct_runs[name] = direct_per_run
  dispatcher_cold_runs[name] = dispatcher_cold_per_run
  dispatcher_warm_runs[name] = dispatcher_warm_per_run
  slow_indirect_cold_runs[name] = slow_indirect_cold_per_run
  cache_runs[name] = cache_per_run
}

BEGIN {
  add_workload("mapped_alu", 128, 0, 1, 1, 0, 0)
  add_workload("memory_read", 64, 0, 1, 1, 0, 0)
  add_workload("branch_chain", 128, 31, 1, 1, 0, 0)
  add_workload("indirect_lookup", 128, 0, 16, 1, 15, 15)
  add_workload("scheduler", 2048, 0, 1, 1, 0, 0)
  add_workload("mixed", 64, 1, 1, 1, 0, 0)
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
  expected_dispatcher = iterations * dispatcher_warm_runs[workload]
  if (phase == "cold")
    expected_dispatcher = iterations * dispatcher_cold_runs[workload]
  expected_direct = direct_runs[workload] * iterations
  expected_slow_indirect = 0
  if (phase == "cold")
    expected_slow_indirect = slow_indirect_cold_runs[workload]
  expected_cache_attempts = cache_runs[workload] * iterations
  expected_cache_hits = phase == "warm" ? expected_cache_attempts : 0

  if (field("result") != "PASS")
    fail(key " was not PASS")
  if ((field("dispatcher_entries") + 0) != expected_dispatcher ||
      (field("lookups") + 0) != expected_dispatcher ||
      (field("helper_terminal") + 0) != expected_dispatcher)
    fail(key " dispatcher/lookup count changed")
  if ((field("direct_chain_attempts") + 0) != expected_direct ||
      (field("direct_chain_hits") + 0) != expected_direct)
    fail(key " direct-chain count changed")
  if ((field("indirect_lookup_hits") + 0) != expected_slow_indirect ||
      (field("indirect_lookup_misses") + 0) != 0)
    fail(key " slow indirect lookup count changed")
  if ((field("indirect_cache_attempts") + 0) != expected_cache_attempts ||
      (field("indirect_cache_hits") + 0) != expected_cache_hits)
    fail(key " cache attempt/hit count changed")
  if ((field("cycle_exits") + 0) != iterations ||
      (field("scheduler_updates") + 0) != iterations ||
      (field("scheduler_exits") + 0) != iterations)
    fail(key " scheduler boundary count changed")
  if ((field("fallthrough_lookup_hits") + 0) != 0 ||
      (field("fallthrough_lookup_misses") + 0) != 0 ||
      (field("fallbacks") + 0) != 0 ||
      (field("execute_arm_calls") + 0) != 0)
    fail(key " used an unexpected slow/fallback path")
}

/ command=rv32im-control-flow / && / case=indirect_miss / {
  miss_seen++
  if (field("result") != "PASS" ||
      (field("dispatcher_entries") + 0) != 1 ||
      (field("indirect_lookup_hits") + 0) != 0 ||
      (field("indirect_lookup_misses") + 0) != 1 ||
      (field("indirect_cache_attempts") + 0) != 1 ||
      (field("indirect_cache_hits") + 0) != 0 ||
      (field("lookups") + 0) != 2 ||
      (field("terminal_calls") + 0) != 1 ||
      (field("fallbacks") + 0) != 1 ||
      (field("relookup_fallbacks") + 0) != 1 ||
      (field("execute_arm_calls") + 0) != 1 ||
      field("reason") != "runtime_miss_counted")
    fail("indirect miss did not take exactly one cache and slow miss")
}

/ command=rv32im-lookup-control-flow / && / case=generation_invalidation / {
  invalidation_seen++
  if (field("result") != "PASS" ||
      (field("cache_attempts") + 0) != 15 ||
      (field("cache_hits") + 0) != 0 ||
      (field("dispatcher_entries") + 0) != 16 ||
      (field("lookups") + 0) != 16 ||
      field("reason") != "generation_rejected_stale_entries")
    fail("generation invalidation reused a stale translated entry")
}

/ command=rv32im-lookup-control-flow / && \
  (/ case=halt_state / || / case=idle_loop / || / case=cpu_alert /) {
  guard = field("case")
  guard_seen[guard]++
  if (field("result") != "PASS" ||
      (field("cache_attempts") + 0) != 1 ||
      (field("cache_hits") + 0) != 0 ||
      (field("dispatcher_entries") + 0) != 1 ||
      (field("scheduler_updates") + 0) != 1 ||
      (field("lookups") + 0) != 1 ||
      (field("terminal_calls") + 0) != 1 ||
      (field("update_calls") + 0) != 1 ||
      field("pc") != "0x08016008" ||
      field("reason") != "cached_edge_deferred_to_scheduler")
    fail(guard " did not reject the cached edge at the scheduler boundary")
  if (guard == "idle_loop" && field("update_cycles") != "0x00000000")
    fail("idle-loop guard did not force cycle exhaustion")
}

END {
  for (wi = 1; wi <= workload_count; wi++) {
    name = workloads[wi]
    if (seen[name ":cold"] != 1 || seen[name ":warm"] != 1)
      fail("missing exactly one cold/warm result for " name)
  }
  if (miss_seen != 1)
    fail("missing exactly one indirect miss result")
  if (invalidation_seen != 1)
    fail("missing exactly one generation invalidation result")
  if (guard_seen["halt_state"] != 1 || guard_seen["idle_loop"] != 1 ||
      guard_seen["cpu_alert"] != 1)
    fail("missing scheduler guard coverage")
  if (summary_seen != 1)
    fail("missing exactly one suite summary")
  if (failed)
    exit 1
  print "result=PASS command=rv32im-lookup-control-flow case=all " \
    "workloads=6 cold_cache_misses=15 warm_cache_hits=1920 " \
    "dispatcher_bypass=1 scheduler_guards=halt,idle,alert " \
    "miss_fallback=1 repeatable=1 " \
    "reason=indirect_lookup_cache_taken_paths_verified"
}
