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
  key_for_window[5] = "memory_write:cold"
  key_for_window[6] = "memory_write:warm"
  key_for_window[7] = "indirect_lookup:cold"
  key_for_window[8] = "indirect_lookup:warm"
  workload[1] = "mapped_alu"
  workload[2] = "memory_read"
  workload[3] = "memory_write"
  workload[4] = "indirect_lookup"
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
  spec_ram_write[key] = field("ram_write_hash")
  spec_io_write[key] = field("io_write_hash")
  spec_alert[key] = field("alert_hash")
  spec_smc[key] = field("smc_hash")
  spec_io_events[key] = field("io_events")
  spec_alert_events[key] = field("alert_events")
  spec_irq_checks[key] = field("irq_checks")
  spec_smc_flushes[key] = field("smc_flushes")
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
  ram_write[prefix ":" key] = field("ram_write_hash")
  io_write[prefix ":" key] = field("io_write_hash")
  alert[prefix ":" key] = field("alert_hash")
  smc[prefix ":" key] = field("smc_hash")
  io_events[prefix ":" key] = field("io_events")
  alert_events[prefix ":" key] = field("alert_events")
  irq_checks[prefix ":" key] = field("irq_checks")
  smc_flushes[prefix ":" key] = field("smc_flushes")
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

  for (wi = 1; wi <= 4; wi++) {
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
            scheduler[observation] != spec_scheduler[key] ||
            ram_write[observation] != spec_ram_write[key] ||
            io_write[observation] != spec_io_write[key] ||
            alert[observation] != spec_alert[key] ||
            smc[observation] != spec_smc[key])
          fail(observation " correctness hash changed")
        if (io_events[observation] != spec_io_events[key] ||
            alert_events[observation] != spec_alert_events[key] ||
            irq_checks[observation] != spec_irq_checks[key] ||
            smc_flushes[observation] != spec_smc_flushes[key])
          fail(observation " write-helper event counts changed")
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
          scheduler["rv32im:" key] != scheduler["mips:" key] ||
          ram_write["rv32im:" key] != ram_write["mips:" key] ||
          io_write["rv32im:" key] != io_write["mips:" key] ||
          alert["rv32im:" key] != alert["mips:" key] ||
          smc["rv32im:" key] != smc["mips:" key])
        fail(key " RV32IM and MIPS guest-visible results diverged")

      if (raw["mips:" key] != spec_mips_raw[key] ||
          generated["mips:" key] != spec_mips_bytes[key])
        fail(key " MIPS reference no longer reproduces the frozen baseline")

      if (name == "mapped_alu")
        mips_bytes_ratio_max_x100 = mapped_mips_bytes_ratio_max_x100
      else if (name == "memory_read")
        mips_bytes_ratio_max_x100 = memory_mips_bytes_ratio_max_x100
      else if (name == "memory_write")
        mips_bytes_ratio_max_x100 = write_mips_bytes_ratio_max_x100
      else
        mips_bytes_ratio_max_x100 = indirect_mips_bytes_ratio_max_x100
      if ((generated["rv32im:" key] + 0) * 10000 > \
          (generated["mips:" key] + 0) * mips_bytes_ratio_max_x100)
        fail(key " RV32IM/MIPS generated-byte ratio exceeded")

      if (name == "mapped_alu") {
        if ((generated["rv32im:" key] + 0) > mapped_bytes_max)
          fail(key " RV32IM mapped-ALU generated-code budget exceeded")
        if ((generated["rv32im:" key] + 0) > (spec_rv_bytes[key] + 0))
          fail(key " RV32IM mapped-ALU generated code regressed")
        if (mode == "cold" && \
            (raw["rv32im:" key] + 0) * 10000 > \
              (spec_rv_raw[key] + 0) * \
                (10000 + mapped_cold_regression_max_x100))
          fail(key " RV32IM mapped-ALU cold instruction budget exceeded")
        if (mode == "warm" && \
            (raw["rv32im:" key] + 0) > (spec_rv_raw[key] + 0))
          fail(key " RV32IM mapped-ALU warm path regressed")
      } else if (name == "memory_read") {
        if ((generated["rv32im:" key] + 0) > memory_bytes_max)
          fail(key " RV32IM generated-code budget exceeded")
        if (mode == "cold" &&
            (raw["rv32im:" key] + 0) > memory_cold_raw_max)
          fail(key " RV32IM cold instruction budget exceeded")
        if (mode == "warm" && \
            (raw["rv32im:" key] + 0) * 100 > \
              (spec_rv_raw[key] + 0) * (100 - memory_min_reduction))
          fail(key " RV32IM warm improvement was below the required reduction")
      } else if (name == "memory_write") {
        if ((generated["rv32im:" key] + 0) > write_bytes_max)
          fail(key " RV32IM write generated-code budget exceeded")
        if (mode == "cold" && \
            (raw["rv32im:" key] + 0) * 10000 > \
              (spec_rv_raw[key] + 0) * \
                (10000 + write_cold_regression_max_x100))
          fail(key " RV32IM write cold instruction budget exceeded")
        if (mode == "warm" && \
            ((spec_rv_raw[key] + 0) - (raw["rv32im:" key] + 0)) * \
                10000 < \
              (spec_rv_raw[key] + 0) * write_warm_min_reduction_x100)
          fail(key " RV32IM write warm improvement was below the required reduction")
      } else {
        if ((generated["rv32im:" key] + 0) > indirect_bytes_max)
          fail(key " RV32IM indirect generated-code budget exceeded")
        if ((generated["rv32im:" key] + 0) > (spec_rv_bytes[key] + 0))
          fail(key " RV32IM indirect generated code regressed")
        if (mode == "cold" && \
            (raw["rv32im:" key] + 0) * 10000 > \
              (spec_rv_raw[key] + 0) * \
                (10000 + lookup_cold_regression_max_x100))
          fail(key " RV32IM indirect cold instruction budget exceeded")
        if (mode == "warm") {
          if (((spec_rv_raw[key] + 0) - (raw["rv32im:" key] + 0)) * \
                10000 < \
              (spec_rv_raw[key] + 0) * lookup_warm_min_reduction_x100)
            fail(key " RV32IM indirect warm improvement was below target")
          baseline_gap = (spec_rv_raw[key] + 0) - \
            (spec_mips_raw[key] + 0)
          optimized_gap = (raw["rv32im:" key] + 0) - \
            (raw["mips:" key] + 0)
          if (optimized_gap < 0)
            optimized_gap = 0
          if (baseline_gap <= 0 || \
              (baseline_gap - optimized_gap) * 10000 < \
                baseline_gap * lookup_gap_min_closure_x100)
            fail(key " RV32IM/MIPS instruction gap did not close enough")
        }
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
        " ram_write_hash=" ram_write["rv32im:" key] \
        " io_write_hash=" io_write["rv32im:" key] \
        " alert_hash=" alert["rv32im:" key] \
        " smc_hash=" smc["rv32im:" key] \
        " io_events=" io_events["rv32im:" key] \
        " alert_events=" alert_events["rv32im:" key] \
        " irq_checks=" irq_checks["rv32im:" key] \
        " smc_flushes=" smc_flushes["rv32im:" key] \
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
  print "result=PASS command=backend-compare workload=all phases=8 " \
    "backends=rv32im,mips exact_guest_work=1 correctness_hashes_equal=1 " \
    "cold_warm_semantics_locked=1 execute_arm_calls=0 " \
    "memory_warm_min_reduction_percent=" memory_min_reduction \
    " mapped_cold_regression_max_percent_x100=" \
      mapped_cold_regression_max_x100 \
    " write_cold_regression_max_percent_x100=" \
      write_cold_regression_max_x100 \
    " write_warm_min_reduction_percent_x100=" \
      write_warm_min_reduction_x100 \
    " lookup_cold_regression_max_percent_x100=" \
      lookup_cold_regression_max_x100 \
    " lookup_warm_min_reduction_percent_x100=" \
      lookup_warm_min_reduction_x100 \
    " lookup_gap_min_closure_percent_x100=" \
      lookup_gap_min_closure_x100 \
    " mapped_mips_bytes_ratio_max_x100=" \
      mapped_mips_bytes_ratio_max_x100 \
    " memory_mips_bytes_ratio_max_x100=" \
      memory_mips_bytes_ratio_max_x100 \
    " write_mips_bytes_ratio_max_x100=" \
      write_mips_bytes_ratio_max_x100 \
    " indirect_mips_bytes_ratio_max_x100=" \
      indirect_mips_bytes_ratio_max_x100 \
    " write_smc_contract_locked=1" \
    " reason=frozen_mips_comparable_baseline_verified"
}
