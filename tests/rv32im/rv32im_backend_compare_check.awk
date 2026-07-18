function field(name, i, pair) {
  for (i = 1; i <= NF; i++) {
    split($i, pair, "=")
    if (pair[1] == name)
      return pair[2]
  }
  return ""
}

function fail(reason) {
  print "RV32IM/MIPS backend comparison failed: " reason > "/dev/stderr"
  failed = 1
}

function prefix_for_file(filename) {
  if (filename == rv_counts_file || filename == rv_log_file)
    return "rv32im"
  if (filename == mips_counts_file || filename == mips_log_file)
    return "mips"
  return ""
}

BEGIN {
  key_for_window[1] = "mapped_alu:cold"
  key_for_window[2] = "mapped_alu:warm"
  key_for_window[3] = "memory_read:cold"
  key_for_window[4] = "memory_read:warm"
  workload[1] = "mapped_alu"
  workload[2] = "memory_read"
  phase[1] = "cold"
  phase[2] = "warm"
}

FILENAME == spec_file && /^baseline=/ {
  spec_header_seen++
  spec_baseline = field("baseline")
  spec_benchmark = field("benchmark_id")
  next
}

FILENAME == spec_file && /^environment=/ {
  spec_environment_seen++
  spec_clang = field("clang_version")
  spec_rv_qemu = field("rv32_qemu_version")
  spec_mips_qemu = field("mips_qemu_version")
  spec_build_config = field("build_config")
  next
}

FILENAME == spec_file && /^workload_manifest_sha256=/ {
  spec_manifest_sha = field("workload_manifest_sha256")
  next
}

FILENAME == spec_file && /^environment_manifest_sha256=/ {
  spec_environment_sha = field("environment_manifest_sha256")
  next
}

FILENAME == spec_file && /^benchmark_source_sha256=/ {
  spec_source_sha = field("benchmark_source_sha256")
  next
}

FILENAME == spec_file && /^workload=/ {
  key = field("workload") ":" field("phase")
  spec_seen[key]++
  spec_guest[key] = field("guest_insns")
  spec_cycles[key] = field("guest_cycles")
  spec_updates[key] = field("update_calls")
  spec_state[key] = field("state_hash")
  spec_memory[key] = field("memory_hash")
  spec_scheduler[key] = field("scheduler_hash")
  spec_rv_raw[key] = field("rv32_qemu_trace_raw")
  spec_mips_raw[key] = field("mips_qemu_trace_raw")
  spec_rv_bytes[key] = field("rv32_generated_bytes")
  spec_mips_bytes[key] = field("mips_generated_bytes")
  next
}

(FILENAME == rv_counts_file || FILENAME == mips_counts_file) && /^window=/ {
  prefix = prefix_for_file(FILENAME)
  window = field("window") + 0
  key = key_for_window[window]
  if (key == "") {
    fail(prefix " emitted an unexpected trace window " window)
    next
  }
  raw_seen[prefix ":" key]++
  raw[prefix ":" key] = field("qemu_trace_raw")
  next
}

(FILENAME == rv_log_file || FILENAME == mips_log_file) &&
  / command=backend-compare-workload / {
  prefix = prefix_for_file(FILENAME)
  key = field("workload") ":" field("phase")
  observation_seen[prefix ":" key]++
  result[prefix ":" key] = field("result")
  backend[prefix ":" key] = field("backend")
  guest[prefix ":" key] = field("guest_insns")
  cycles[prefix ":" key] = field("guest_cycles")
  generated[prefix ":" key] = field("generated_bytes")
  added[prefix ":" key] = field("code_bytes_added")
  state[prefix ":" key] = field("state_hash")
  memory[prefix ":" key] = field("memory_hash")
  scheduler[prefix ":" key] = field("scheduler_hash")
  updates[prefix ":" key] = field("update_calls")
  update_cycles[prefix ":" key] = field("update_cycles")
  interp_calls[prefix ":" key] = field("execute_arm_calls")
  harness[prefix ":" key] = field("harness_mode")
  next
}

(FILENAME == rv_log_file || FILENAME == mips_log_file) &&
  / command=backend-compare / && !/ command=backend-compare-workload / {
  prefix = prefix_for_file(FILENAME)
  summary_seen[prefix]++
  summary_result[prefix] = field("result")
  summary_calls[prefix] = field("execute_arm_calls")
  summary_native_blocks[prefix] = field("native_blocks")
  summary_native_ops[prefix] = field("native_ops")
  summary_fallbacks[prefix] = field("fallbacks")
  next
}

END {
  if (spec_header_seen != 1 || spec_baseline != baseline_id ||
      spec_benchmark != benchmark_id)
    fail("frozen baseline identity changed")
  if (spec_environment_seen != 1 || spec_clang != clang_version ||
      spec_rv_qemu != rv_qemu_version ||
      spec_mips_qemu != mips_qemu_version ||
      spec_build_config != build_config)
    fail("frozen toolchain environment changed")
  if (spec_manifest_sha != manifest_sha ||
      spec_environment_sha != environment_sha ||
      spec_source_sha != source_sha)
    fail("frozen manifest or benchmark source changed")

  for (wi = 1; wi <= 2; wi++) {
    for (pi = 1; pi <= 2; pi++) {
      name = workload[wi]
      mode = phase[pi]
      key = name ":" mode
      if (spec_seen[key] != 1) {
        fail(key " missing frozen observation")
        continue
      }

      for (bi = 1; bi <= 2; bi++) {
        prefix = bi == 1 ? "rv32im" : "mips"
        observation = prefix ":" key
        if (raw_seen[observation] != 1 ||
            observation_seen[observation] != 1) {
          fail(observation " missing or duplicated observation")
          continue
        }
        if (result[observation] != "PASS" ||
            backend[observation] != prefix)
          fail(observation " did not execute the selected native backend")
        expected_harness = (prefix == "rv32im") ? \
          "cpu_threaded_frontend_rv32im" : "cpu_threaded_frontend_mips"
        if (harness[observation] != expected_harness)
          fail(observation " harness mode changed")
        if (guest[observation] != spec_guest[key] ||
            cycles[observation] != spec_cycles[key])
          fail(observation " guest work changed")
        if (state[observation] != spec_state[key] ||
            memory[observation] != spec_memory[key] ||
            scheduler[observation] != spec_scheduler[key])
          fail(observation " correctness hash changed")
        if (updates[observation] != spec_updates[key] ||
            update_cycles[observation] != "0x00000000")
          fail(observation " scheduler boundary changed")
        if ((interp_calls[observation] + 0) != 0)
          fail(observation " used execute_arm fallback")
        if (mode == "cold" && added[observation] != generated[observation])
          fail(observation " cold translation bytes were not newly emitted")
        if (mode == "warm" && (added[observation] + 0) != 0)
          fail(observation " warm run translated new code")
      }

      if (state["rv32im:" key] != state["mips:" key] ||
          memory["rv32im:" key] != memory["mips:" key] ||
          scheduler["rv32im:" key] != scheduler["mips:" key])
        fail(key " RV32IM and MIPS guest-visible results diverged")

      if (raw["mips:" key] != spec_mips_raw[key] ||
          generated["mips:" key] != spec_mips_bytes[key])
        fail(key " MIPS reference no longer reproduces the frozen baseline")

      if (name == "mapped_alu") {
        if (raw["rv32im:" key] != spec_rv_raw[key] ||
            generated["rv32im:" key] != spec_rv_bytes[key])
          fail(key " RV32IM non-memory guard changed")
      } else {
        if ((generated["rv32im:" key] + 0) > memory_bytes_max)
          fail(key " RV32IM generated-code budget exceeded")
        if (mode == "cold" &&
            (raw["rv32im:" key] + 0) > memory_cold_raw_max)
          fail(key " RV32IM cold instruction budget exceeded")
        if (mode == "warm" && \
            (raw["rv32im:" key] + 0) * 100 > \
              (spec_rv_raw[key] + 0) * (100 - memory_min_reduction))
          fail(key " RV32IM warm improvement was below the required reduction")
      }

      raw_ratio = int((raw["rv32im:" key] + 0) * 10000 / \
                      (raw["mips:" key] + 0))
      byte_ratio = int((generated["rv32im:" key] + 0) * 10000 / \
                       (generated["mips:" key] + 0))
      reduction = int(((spec_rv_raw[key] + 0) - \
                       (raw["rv32im:" key] + 0)) * 10000 / \
                      (spec_rv_raw[key] + 0))
      report[++report_count] = "result=PASS command=backend-compare" \
        " workload=" name " phase=" mode \
        " guest_insns=" guest["rv32im:" key] \
        " guest_cycles=" cycles["rv32im:" key] \
        " rv32_qemu_trace_raw=" raw["rv32im:" key] \
        " mips_qemu_trace_raw=" raw["mips:" key] \
        " rv32_to_mips_exec_percent_x100=" raw_ratio \
        " rv32_baseline_reduction_percent_x100=" reduction \
        " rv32_generated_bytes=" generated["rv32im:" key] \
        " mips_generated_bytes=" generated["mips:" key] \
        " rv32_to_mips_bytes_percent_x100=" byte_ratio \
        " state_hash=" state["rv32im:" key] \
        " memory_hash=" memory["rv32im:" key] \
        " scheduler_hash=" scheduler["rv32im:" key] \
        " counter_source=qemu_exec_trace" \
        " counter_semantics=qemu_tb_instruction_sum" \
        " reason=real_frontend_guest_work_equal"
    }
  }

  for (bi = 1; bi <= 2; bi++) {
    prefix = bi == 1 ? "rv32im" : "mips"
    if (summary_seen[prefix] != 1 || summary_result[prefix] != "PASS" ||
        (summary_calls[prefix] + 0) != 0)
      fail(prefix " aggregate native execution proof failed")
  }
  if ((summary_native_blocks["rv32im"] + 0) == 0 ||
      (summary_native_ops["rv32im"] + 0) == 0 ||
      (summary_fallbacks["rv32im"] + 0) != 0)
    fail("RV32IM native counters did not prove fallback-free execution")

  if (failed)
    exit 1
  for (ri = 1; ri <= report_count; ri++)
    print report[ri]
  print "result=PASS command=backend-compare workload=all phases=4 " \
    "backends=rv32im,mips exact_guest_work=1 correctness_hashes_equal=1 " \
    "cold_warm_semantics_locked=1 execute_arm_calls=0 " \
    "memory_warm_min_reduction_percent=" memory_min_reduction \
    " reason=frozen_mips_comparable_baseline_verified"
}
