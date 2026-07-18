function field(name, i, kv) {
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    if (kv[1] == name)
      return kv[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im perf check failed: " reason > "/dev/stderr"
  failed = 1
}

BEGIN {
  expected["mapped_alu:cold"] = 1
  expected["mapped_alu:warm"] = 1
  expected["memory_read:cold"] = 1
  expected["memory_read:warm"] = 1
  expected["branch_chain:cold"] = 1
  expected["branch_chain:warm"] = 1
  expected["scheduler:cold"] = 1
  expected["scheduler:warm"] = 1
  expected["mixed:cold"] = 1
  expected["mixed:warm"] = 1

  guest_insns["mapped_alu:cold"] = 256
  guest_insns["mapped_alu:warm"] = 32768
  guest_insns["memory_read:cold"] = 48
  guest_insns["memory_read:warm"] = 3072
  guest_insns["branch_chain:cold"] = 63
  guest_insns["branch_chain:warm"] = 8064
  guest_insns["scheduler:cold"] = 1
  guest_insns["scheduler:warm"] = 2048
  guest_insns["mixed:cold"] = 61
  guest_insns["mixed:warm"] = 3904

  state_hash["mapped_alu:cold"] = "0x011b51b6"
  state_hash["mapped_alu:warm"] = "0x89018a9d"
  state_hash["memory_read:cold"] = "0xb9e5c390"
  state_hash["memory_read:warm"] = "0xb9e5c390"
  state_hash["branch_chain:cold"] = "0x7615e4db"
  state_hash["branch_chain:warm"] = "0x1c8bb6b0"
  state_hash["scheduler:cold"] = "0xee4b6d34"
  state_hash["scheduler:warm"] = "0xee4b6d34"
  state_hash["mixed:cold"] = "0x403e59d4"
  state_hash["mixed:warm"] = "0x403e59d4"

  memory_hash["mapped_alu:cold"] = "0x811c9dc5"
  memory_hash["mapped_alu:warm"] = "0x811c9dc5"
  memory_hash["memory_read:cold"] = "0x5fbb5540"
  memory_hash["memory_read:warm"] = "0x28f6afc5"
  memory_hash["branch_chain:cold"] = "0x811c9dc5"
  memory_hash["branch_chain:warm"] = "0x811c9dc5"
  memory_hash["scheduler:cold"] = "0x811c9dc5"
  memory_hash["scheduler:warm"] = "0x811c9dc5"
  memory_hash["mixed:cold"] = "0xc45006d7"
  memory_hash["mixed:warm"] = "0xb8a06245"

  scheduler_hash["mapped_alu:cold"] = "0x1c430938"
  scheduler_hash["mapped_alu:warm"] = "0xaaeb39c5"
  scheduler_hash["memory_read:cold"] = "0x084354f4"
  scheduler_hash["memory_read:warm"] = "0x1e4d33c5"
  scheduler_hash["branch_chain:cold"] = "0x1171fe38"
  scheduler_hash["branch_chain:warm"] = "0x35da79c5"
  scheduler_hash["scheduler:cold"] = "0x80d993b0"
  scheduler_hash["scheduler:warm"] = "0xe08f9dc5"
  scheduler_hash["mixed:cold"] = "0x76e1433c"
  scheduler_hash["mixed:warm"] = "0xea4b69c5"

  trace_hash["mapped_alu:cold"] = "0x2d938744"
  trace_hash["mapped_alu:warm"] = "0x70d241c5"
  trace_hash["memory_read:cold"] = "0x3f285294"
  trace_hash["memory_read:warm"] = "0xc648afc5"
  trace_hash["branch_chain:cold"] = "0x0a69f0a4"
  trace_hash["branch_chain:warm"] = "0x23e541c5"
  trace_hash["scheduler:cold"] = "0xe7405a04"
  trace_hash["scheduler:warm"] = "0xf316ddc5"
  trace_hash["mixed:cold"] = "0x1bfebbf4"
  trace_hash["mixed:warm"] = "0x3bea2fc5"

  cold_exec_max["mapped_alu"] = mapped_alu_cold_exec_max
  cold_exec_max["memory_read"] = memory_read_cold_exec_max
  cold_exec_max["branch_chain"] = branch_chain_cold_exec_max
  cold_exec_max["scheduler"] = scheduler_cold_exec_max
  cold_exec_max["mixed"] = mixed_cold_exec_max
  warm_exec_max["mapped_alu"] = mapped_alu_exec_max
  warm_exec_max["memory_read"] = memory_read_exec_max
  warm_exec_max["branch_chain"] = branch_chain_exec_max
  warm_exec_max["scheduler"] = scheduler_exec_max
  warm_exec_max["mixed"] = mixed_exec_max
  bytes_max["mapped_alu"] = mapped_alu_bytes_max
  bytes_max["memory_read"] = memory_read_bytes_max
  bytes_max["branch_chain"] = branch_chain_bytes_max
  bytes_max["scheduler"] = scheduler_bytes_max
  bytes_max["mixed"] = mixed_bytes_max
}

/ command=rv32im-perf / {
  result = field("result")
  workload = field("workload")
  phase = field("phase")

  if (result != "PASS")
    fail("non-PASS line for " workload ":" phase)

  key = workload ":" phase
  if (key in expected) {
    seen[key]++
    if (field("counter_source") != "qemu_exec_trace")
      fail(key " missing deterministic qemu instruction source")
    if (field("counter_semantics") != "qemu_tb_instruction_sum")
      fail(key " missing executed-instruction semantics")
    if (field("rdinstret_csr_verified") != "0")
      fail(key " did not label rdinstret diagnostic-only")
    expected_scope = phase == "cold" ? \
      "translate_sync_first_execute" : "steady_state_execute"
    if (field("measurement_scope") != expected_scope)
      fail(key " measurement scope changed")
    if ((field("guest_insns") + 0) <= 0)
      fail(key " missing guest instructions")
    if ((field("guest_insns") + 0) != guest_insns[key])
      fail(key " guest instruction count changed")
    if ((field("executed_rv32_insns") + 0) <= 0)
      fail(key " missing executed RV32 instructions")
    if ((field("executed_rv32_insns_raw") + 0) - \
        (field("executed_rv32_insns_overhead") + 0) != \
        (field("executed_rv32_insns") + 0))
      fail(key " executed-instruction adjustment changed")
    if ((field("generated_blocks") + 0) <= 0 ||
        (field("generated_bytes") + 0) <= 0)
      fail(key " missing generated-code evidence")
    if ((field("fallbacks") + 0) != 0 ||
        (field("execute_arm_calls") + 0) != 0)
      fail(key " used an interpreter fallback")
    if (field("state_hash") == "" || field("memory_hash") == "" ||
        field("scheduler_hash") == "" || field("trace_hash") == "")
      fail(key " missing correctness hashes")
    if (field("state_hash") != state_hash[key] ||
        field("memory_hash") != memory_hash[key] ||
        field("scheduler_hash") != scheduler_hash[key] ||
        field("trace_hash") != trace_hash[key])
      fail(key " correctness hash changed")
    if ((field("generated_bytes") + 0) > bytes_max[workload])
      fail(key " generated bytes exceeded ceiling")
    if (phase == "cold")
      exec_max = cold_exec_max[workload]
    else
      exec_max = warm_exec_max[workload]
    if ((field("executed_rv32_insns") + 0) > exec_max)
      fail(key " executed RV32 instructions exceeded ceiling")
    if (phase == "cold" && (field("translations") + 0) <= 0)
      fail(key " did not include translation")
    if (phase == "warm" && (field("translations") + 0) != 0)
      fail(key " unexpectedly translated code")
  }

  if (workload == "control" && phase == "probe")
    control_seen++
  if (workload == "empty" && phase == "control") {
    empty_seen++
    if ((field("executed_rv32_insns_raw") + 0) <= 0 ||
        field("executed_rv32_insns_raw") != \
          field("executed_rv32_insns_overhead") ||
        field("executed_rv32_insns") != "0")
      fail("invalid empty-window adjustment")
  }
  if (workload == "mapped_alu_encoding" && phase == "check") {
    encoding_seen++
    if ((field("cases") + 0) != 13 ||
        field("reason") != "exact_mapped_alu_encodings")
      fail("mapped ALU exact-encoding coverage changed")
  }
  if (workload == "mapped_alu" && phase == "warm") {
    mapped_alu_optimized_exec = field("executed_rv32_insns") + 0
    mapped_alu_optimized_bytes = field("generated_bytes") + 0
  }
  if (workload == "all" && phase == "summary")
    summary_seen++
}

END {
  for (key in expected) {
    if (seen[key] != 1)
      fail("expected exactly one " key " result")
  }
  if (control_seen != 1)
    fail("missing instruction-counter control result")
  if (empty_seen != 1)
    fail("missing measurement overhead result")
  if (encoding_seen != 1)
    fail("missing exact mapped ALU encoding result")
  if (summary_seen != 1)
    fail("missing suite summary")
  if (mapped_alu_optimized_exec * 100 > \
      mapped_alu_baseline_exec * (100 - mapped_alu_min_improvement))
    fail("mapped ALU warm executed-instruction improvement below target")
  if (mapped_alu_optimized_bytes >= mapped_alu_baseline_bytes)
    fail("mapped ALU generated bytes did not decrease")
  if (failed)
    exit 1
  improvement_x100 = int( \
    (mapped_alu_baseline_exec - mapped_alu_optimized_exec) * 10000 / \
    mapped_alu_baseline_exec)
  byte_improvement_x100 = int( \
    (mapped_alu_baseline_bytes - mapped_alu_optimized_bytes) * 10000 / \
    mapped_alu_baseline_bytes)
  print "RV32IM mapped_alu baseline warm_executed_rv32_insns=" mapped_alu_baseline_exec \
    " generated_bytes=" mapped_alu_baseline_bytes
  print "RV32IM mapped_alu optimized warm_executed_rv32_insns=" \
    mapped_alu_optimized_exec " generated_bytes=" \
    mapped_alu_optimized_bytes " executed_insns_reduction_percent_x100=" \
    improvement_x100 " byte_reduction_percent_x100=" byte_improvement_x100
  print "RV32IM deterministic perf check passed"
}
