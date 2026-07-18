function field(name, i, kv) {
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    if (kv[1] == name)
      return kv[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im Armwrestler perf check failed: " reason > "/dev/stderr"
  failed = 1
}

function capture_result(profile, test, prefix) {
  prefix = profile ":" test
  result_seen[prefix]++
  result_value[prefix] = field("result")
  result_expected[prefix] = field("expected_results")
  result_observed[prefix] = field("observed_results")
  result_mask[prefix] = field("failure_mask")
  result_code[prefix] = field("code_bytes")
  result_blocks[prefix] = field("blocks_emitted")
  result_native_blocks[prefix] = field("native_blocks")
  result_fallbacks[prefix] = field("fallbacks")
  result_fallback_events[prefix] = field("fallback_events")
  result_interp_calls[prefix] = field("execute_arm_calls")
  result_native_data_proc[prefix] = field("native_data_proc")
  result_native_branch[prefix] = field("native_branch")
  result_native_load[prefix] = field("native_load")
  result_native_store[prefix] = field("native_store")
  result_native_psr[prefix] = field("native_psr")
  result_thumb_helpers[prefix] = field("thumb_helpers")
  result_trace_count[prefix] = field("trace_count")
  result_trace_hash[prefix] = field("trace_hash")
  result_update_calls[prefix] = field("update_calls")
  result_warm_replays[prefix] = field("warm_replays")
  result_warm_observed[prefix] = field("warm_observed_results")
  result_warm_mask[prefix] = field("warm_failure_mask")
  result_warm_code_added[prefix] = field("warm_code_bytes_added")
  result_jit_profile[prefix] = field("jit_profile")
  result_mapped_alu_enabled[prefix] = field("mapped_alu_fastpath_enabled")
  result_fast_ram_reads_enabled[prefix] = field("fast_ram_reads_enabled")
  result_fast_ram_stores_enabled[prefix] = field("fast_ram_stores_enabled")
  result_harness[prefix] = field("harness_mode")
}

function require_equal(profile, test, metric, lhs, rhs) {
  if (lhs != rhs)
    fail(profile ":" test " changed " metric " (" lhs " != " rhs ")")
}

BEGIN {
  tests[1] = "arm0"
  tests[2] = "arm1"
  tests[3] = "arm2"
  tests[4] = "arm3"
  tests[5] = "arm4"
  tests[6] = "thumb0"
  tests[7] = "thumb1"
  tests[8] = "thumb2"
}

FILENAME == spec_file && /^benchmark_id=/ {
  spec_header_seen++
  spec_benchmark_id = field("benchmark_id")
  spec_rom_sha = field("rom_sha256")
  spec_clang = field("clang_version")
  spec_qemu = field("qemu_version")
  spec_build = field("build_config")
  spec_environment_sha = field("environment_manifest_sha256")
  spec_source = field("counter_source")
  spec_semantics = field("counter_semantics")
  spec_rdinstret_verified = field("rdinstret_csr_verified")
  spec_text_sha = field("text_sha256")
  spec_optimization = field("isolated_optimization")
  spec_baseline_mapped_alu = field("baseline_mapped_alu_fastpath_enabled")
  spec_baseline_fast_ram_reads = field("baseline_fast_ram_reads_enabled")
  spec_baseline_fast_ram_stores = field("baseline_fast_ram_stores_enabled")
  next
}

FILENAME == spec_file && /^test=/ {
  test = field("test")
  spec_seen[test]++
  spec_mode[test] = field("mode")
  spec_expected[test] = field("expected_results")
  spec_cold[test] = field("cold_executed_rv32_insns")
  spec_warm[test] = field("warm_executed_rv32_insns")
  spec_code[test] = field("generated_bytes")
  next
}

FILENAME == spec_file && /^summary=baseline/ {
  spec_summary_seen++
  spec_summary_cold = field("cold_executed_rv32_insns")
  spec_summary_warm = field("warm_executed_rv32_insns")
  spec_arm_code = field("arm_generated_bytes")
  spec_thumb_code = field("thumb_generated_bytes")
  next
}

(FILENAME == baseline_counts_file || FILENAME == optimized_counts_file) && /^window=/ {
  profile = FILENAME == baseline_counts_file ? "baseline" : "optimized"
  window = field("window") + 0
  if (window < 1 || window > 16) {
    fail(profile " had an invalid trace window")
    next
  }
  count_seen[profile ":" window]++
  count_value[profile ":" window] = field("qemu_trace_raw")
  next
}

(FILENAME == baseline_log_file || FILENAME == optimized_log_file) && / command=armwrestler-jit-test / {
  profile = FILENAME == baseline_log_file ? "baseline" : "optimized"
  capture_result(profile, field("test"))
  next
}

(FILENAME == baseline_log_file || FILENAME == optimized_log_file) && / command=armwrestler test=all / {
  profile = FILENAME == baseline_log_file ? "baseline" : "optimized"
  aggregate_seen[profile]++
  aggregate_result[profile] = field("result")
  aggregate_expected[profile] = field("expected_results")
  aggregate_observed[profile] = field("observed_results")
  aggregate_mask[profile] = field("failure_mask")
  aggregate_blocks[profile] = field("blocks_emitted")
  aggregate_native_blocks[profile] = field("native_blocks")
  aggregate_arm_code[profile] = field("arm_code_bytes_total")
  aggregate_thumb_code[profile] = field("thumb_code_bytes_total")
  aggregate_fallbacks[profile] = field("fallbacks")
  aggregate_fallback_events[profile] = field("fallback_events")
  aggregate_interp_calls[profile] = field("execute_arm_calls")
  aggregate_trace_count[profile] = field("trace_count")
  aggregate_trace_hash[profile] = field("trace_hash")
  aggregate_update_calls[profile] = field("update_calls")
  aggregate_warm_replays[profile] = field("warm_replays")
  aggregate_warm_code_added[profile] = field("warm_code_bytes_added")
  aggregate_jit_profile[profile] = field("jit_profile")
  aggregate_mapped_alu_enabled[profile] = field("mapped_alu_fastpath_enabled")
  aggregate_fast_ram_reads_enabled[profile] = field("fast_ram_reads_enabled")
  aggregate_fast_ram_stores_enabled[profile] = field("fast_ram_stores_enabled")
  aggregate_harness[profile] = field("harness_mode")
  next
}

END {
  if (spec_header_seen != 1 || spec_summary_seen != 1)
    fail("baseline specification header/summary was missing")
  if (spec_benchmark_id != benchmark_id || spec_rom_sha != rom_sha ||
      spec_clang != clang_version || spec_qemu != qemu_version ||
      spec_build != build_config || spec_environment_sha != environment_sha ||
      spec_text_sha != text_sha)
    fail("locked environment, ROM, or text identity changed")
  if (spec_source != "qemu_exec_trace" ||
      spec_semantics != "qemu_tb_instruction_sum" ||
      spec_rdinstret_verified != "0")
    fail("counter source/semantics were mislabeled")
  if (spec_optimization != isolated_optimization ||
      spec_baseline_mapped_alu != baseline_mapped_alu_enabled ||
      spec_baseline_fast_ram_reads != baseline_fast_ram_reads_enabled ||
      spec_baseline_fast_ram_stores != baseline_fast_ram_stores_enabled)
    fail("frozen baseline did not isolate the selected optimization")

  for (ti = 1; ti <= 8; ti++) {
    test = tests[ti]
    mode = ti <= 5 ? "arm" : "thumb"
    cold_window = (ti * 2) - 1
    warm_window = ti * 2

    if (spec_seen[test] != 1 || spec_mode[test] != mode)
      fail(test " was missing from the frozen baseline")

    for (pi = 1; pi <= 2; pi++) {
      profile = pi == 1 ? "baseline" : "optimized"
      prefix = profile ":" test
      if (count_seen[profile ":" cold_window] != 1 ||
          count_seen[profile ":" warm_window] != 1)
        fail(prefix " did not have one cold and one warm trace window")
      if (result_seen[prefix] != 1 || result_value[prefix] != "PASS")
        fail(prefix " did not have one PASS result")
      if (result_expected[prefix] != spec_expected[test] ||
          result_observed[prefix] != spec_expected[test] ||
          result_mask[prefix] != "0x00000000")
        fail(prefix " changed the Armwrestler result contract")
      if ((result_native_blocks[prefix] + 0) <= 0 ||
          (result_fallbacks[prefix] + 0) != 0 ||
          (result_fallback_events[prefix] + 0) != 0 ||
          (result_interp_calls[prefix] + 0) != 0)
        fail(prefix " lacked native-only execution evidence")
      if (result_warm_replays[prefix] != "1" ||
          result_warm_observed[prefix] != spec_expected[test] ||
          result_warm_mask[prefix] != "0x00000000" ||
          result_warm_code_added[prefix] != "0")
        fail(prefix " warm replay was not a cache-stable equivalent run")
      expected_fast_ram_reads = profile == "baseline" ? \
        baseline_fast_ram_reads_enabled : optimized_fast_ram_reads_enabled
      expected_fast_ram_stores = profile == "baseline" ? \
        baseline_fast_ram_stores_enabled : optimized_fast_ram_stores_enabled
      if (expected_fast_ram_stores == "na")
        expected_fast_ram_stores = ""
      if (result_jit_profile[prefix] != jit_profile ||
          result_mapped_alu_enabled[prefix] != baseline_mapped_alu_enabled ||
          result_fast_ram_reads_enabled[prefix] != expected_fast_ram_reads ||
          result_fast_ram_stores_enabled[prefix] != expected_fast_ram_stores ||
          result_harness[prefix] != "armwrestler_frontend_jit_only")
        fail(prefix " did not run the isolated selector profile")
    }

    if (count_value["baseline:" cold_window] != spec_cold[test] ||
        count_value["baseline:" warm_window] != spec_warm[test] ||
        result_code["baseline:" test] != spec_code[test])
      fail(test " no longer reproduces the frozen baseline")
    if ((result_code["optimized:" test] + 0) > (result_code["baseline:" test] + 0))
      fail(test " generated code size regressed")
    if ((count_value["optimized:" warm_window] + 0) > \
        (count_value["baseline:" warm_window] + 0))
      fail(test " regressed warm execution")
    if ((count_value["optimized:" cold_window] + 0) * 10000 > \
        (count_value["baseline:" cold_window] + 0) * \
          (10000 + cold_per_test_regression_max_x100))
      fail(test " cold translation/execution exceeded the per-test budget")

    require_equal("optimized", test, "blocks_emitted",
                  result_blocks["optimized:" test],
                  result_blocks["baseline:" test])
    require_equal("optimized", test, "native blocks",
                  result_native_blocks["optimized:" test],
                  result_native_blocks["baseline:" test])
    require_equal("optimized", test, "native data-processing count",
                  result_native_data_proc["optimized:" test],
                  result_native_data_proc["baseline:" test])
    require_equal("optimized", test, "native branch count",
                  result_native_branch["optimized:" test],
                  result_native_branch["baseline:" test])
    require_equal("optimized", test, "native load count",
                  result_native_load["optimized:" test],
                  result_native_load["baseline:" test])
    require_equal("optimized", test, "native store count",
                  result_native_store["optimized:" test],
                  result_native_store["baseline:" test])
    require_equal("optimized", test, "native PSR count",
                  result_native_psr["optimized:" test],
                  result_native_psr["baseline:" test])
    require_equal("optimized", test, "Thumb helper count",
                  result_thumb_helpers["optimized:" test],
                  result_thumb_helpers["baseline:" test])
    require_equal("optimized", test, "guest trace count",
                  result_trace_count["optimized:" test],
                  result_trace_count["baseline:" test])
    require_equal("optimized", test, "guest trace hash",
                  result_trace_hash["optimized:" test],
                  result_trace_hash["baseline:" test])

    for (phase_i = 1; phase_i <= 2; phase_i++) {
      phase = phase_i == 1 ? "cold" : "warm"
      window = phase_i == 1 ? cold_window : warm_window
      baseline = count_value["baseline:" window] + 0
      optimized = count_value["optimized:" window] + 0
      reduction = int((baseline - optimized) * 10000 / baseline)
      direction = optimized < baseline ? "improved" : \
          (optimized > baseline ? "regressed" : "unchanged")
      report[++report_count] = "result=PASS command=armwrestler-perf test=" test \
          " mode=" mode " phase=" phase \
          " baseline_executed_rv32_insns=" baseline \
          " optimized_executed_rv32_insns=" optimized \
          " executed_insns_reduction_percent_x100=" reduction \
          " performance_direction=" direction \
          " baseline_generated_bytes=" result_code["baseline:" test] \
          " optimized_generated_bytes=" result_code["optimized:" test] \
          " counter_source=qemu_exec_trace counter_semantics=qemu_tb_instruction_sum" \
          " rdinstret_csr_verified=0 reason=real_frontend_workload_equal"
      totals["baseline:" mode ":" phase] += baseline
      totals["optimized:" mode ":" phase] += optimized
      totals["baseline:all:" phase] += baseline
      totals["optimized:all:" phase] += optimized
    }
  }

  for (pi = 1; pi <= 2; pi++) {
    profile = pi == 1 ? "baseline" : "optimized"
    expected_fast_ram_reads = profile == "baseline" ? \
      baseline_fast_ram_reads_enabled : optimized_fast_ram_reads_enabled
    expected_fast_ram_stores = profile == "baseline" ? \
      baseline_fast_ram_stores_enabled : optimized_fast_ram_stores_enabled
    if (expected_fast_ram_stores == "na")
      expected_fast_ram_stores = ""
    if (aggregate_seen[profile] != 1 || aggregate_result[profile] != "PASS" ||
        aggregate_expected[profile] != "79" ||
        aggregate_observed[profile] != "79" ||
        aggregate_mask[profile] != "0x00000000" ||
        aggregate_native_blocks[profile] != "356" ||
        aggregate_fallbacks[profile] != "0" ||
        aggregate_fallback_events[profile] != "0" ||
        aggregate_interp_calls[profile] != "0" ||
        aggregate_warm_replays[profile] != "8" ||
        aggregate_warm_code_added[profile] != "0" ||
        aggregate_jit_profile[profile] != jit_profile ||
        aggregate_mapped_alu_enabled[profile] != baseline_mapped_alu_enabled ||
        aggregate_fast_ram_reads_enabled[profile] != expected_fast_ram_reads ||
        aggregate_fast_ram_stores_enabled[profile] != expected_fast_ram_stores ||
        aggregate_harness[profile] != "armwrestler_frontend_jit_only")
      fail(profile " aggregate native/correctness contract changed")
  }
  if (aggregate_arm_code["baseline"] != spec_arm_code ||
      aggregate_thumb_code["baseline"] != spec_thumb_code ||
      totals["baseline:all:cold"] != spec_summary_cold ||
      totals["baseline:all:warm"] != spec_summary_warm)
    fail("aggregate baseline no longer matches the frozen specification")
  if ((aggregate_arm_code["optimized"] + 0) > \
        (aggregate_arm_code["baseline"] + 0) ||
      (aggregate_thumb_code["optimized"] + 0) > \
        (aggregate_thumb_code["baseline"] + 0) ||
      (aggregate_arm_code["optimized"] + aggregate_thumb_code["optimized"]) >= \
        (aggregate_arm_code["baseline"] + aggregate_thumb_code["baseline"]))
    fail("aggregate code size regressed or did not improve overall")
  if (totals["optimized:all:cold"] * 10000 > \
      totals["baseline:all:cold"] * \
        (10000 + cold_aggregate_regression_max_x100))
    fail("aggregate cold translation/execution exceeded its budget")
  if ((totals["baseline:all:warm"] - totals["optimized:all:warm"]) * \
        10000 < totals["baseline:all:warm"] * warm_min_reduction_x100)
    fail("aggregate warm improvement was below the required reduction")
  if (totals["optimized:arm:warm"] > totals["baseline:arm:warm"] ||
      totals["optimized:thumb:warm"] > totals["baseline:thumb:warm"])
    fail("ARM or Thumb aggregate warm execution regressed")
  require_equal("optimized", "all", "blocks_emitted",
                aggregate_blocks["optimized"], aggregate_blocks["baseline"])
  require_equal("optimized", "all", "native blocks",
                aggregate_native_blocks["optimized"],
                aggregate_native_blocks["baseline"])
  require_equal("optimized", "all", "guest trace count",
                aggregate_trace_count["optimized"],
                aggregate_trace_count["baseline"])
  require_equal("optimized", "all", "guest trace hash",
                aggregate_trace_hash["optimized"],
                aggregate_trace_hash["baseline"])
  require_equal("optimized", "all", "scheduler updates",
                aggregate_update_calls["optimized"],
                aggregate_update_calls["baseline"])

  if (failed)
    exit 1

  for (ri = 1; ri <= report_count; ri++)
    print report[ri]
  for (mi = 1; mi <= 3; mi++) {
    mode = mi == 1 ? "arm" : (mi == 2 ? "thumb" : "all")
    for (phase_i = 1; phase_i <= 2; phase_i++) {
      phase = phase_i == 1 ? "cold" : "warm"
      baseline = totals["baseline:" mode ":" phase]
      optimized = totals["optimized:" mode ":" phase]
      reduction = int((baseline - optimized) * 10000 / baseline)
      direction = optimized < baseline ? "improved" : \
          (optimized > baseline ? "regressed" : "unchanged")
      print "result=PASS command=armwrestler-perf test=" mode \
          " phase=" phase \
          " baseline_executed_rv32_insns=" baseline \
          " optimized_executed_rv32_insns=" optimized \
          " executed_insns_reduction_percent_x100=" reduction \
          " performance_direction=" direction \
          " baseline_arm_generated_bytes=" aggregate_arm_code["baseline"] \
          " optimized_arm_generated_bytes=" aggregate_arm_code["optimized"] \
          " baseline_thumb_generated_bytes=" aggregate_thumb_code["baseline"] \
          " optimized_thumb_generated_bytes=" aggregate_thumb_code["optimized"] \
          " counter_source=qemu_exec_trace counter_semantics=qemu_tb_instruction_sum" \
          " rdinstret_csr_verified=0 text_sha256=" text_sha \
          " reason=real_frontend_aggregate_compared"
    }
  }
  print "result=PASS command=armwrestler-perf test=all phase=summary" \
      " benchmark_id=" benchmark_id " tests=8 expected_results=79" \
      " observed_results=79 failure_mask=0x00000000 fallbacks=0" \
      " fallback_events=0 execute_arm_calls=0 warm_replays=8" \
      " warm_code_bytes_added=0 counter_source=qemu_exec_trace" \
      " counter_semantics=qemu_tb_instruction_sum rdinstret_csr_verified=0" \
      " cold_per_test_regression_max_percent_x100=" \
        cold_per_test_regression_max_x100 \
      " cold_aggregate_regression_max_percent_x100=" \
        cold_aggregate_regression_max_x100 \
      " warm_min_reduction_percent_x100=" warm_min_reduction_x100 \
      " isolated_optimization=" isolated_optimization \
      " mapped_alu_fastpath_enabled=" baseline_mapped_alu_enabled \
      " baseline_fast_ram_reads_enabled=" baseline_fast_ram_reads_enabled \
      " optimized_fast_ram_reads_enabled=" optimized_fast_ram_reads_enabled \
      " baseline_fast_ram_stores_enabled=" baseline_fast_ram_stores_enabled \
      " optimized_fast_ram_stores_enabled=" optimized_fast_ram_stores_enabled \
      " repeatability=byte_exact" \
      " environment_manifest_sha256=" environment_sha \
      " text_sha256=" text_sha " reason=real_frontend_ab_verified"
}
