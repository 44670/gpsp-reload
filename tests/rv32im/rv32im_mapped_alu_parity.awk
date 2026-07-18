function field(name, i, kv) {
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    if (kv[1] == name)
      return kv[2]
  }
  return ""
}

function fail(reason) {
  print "rv32im mapped ALU parity failed: " reason > "/dev/stderr"
  failed = 1
}

BEGIN {
  expected["add"] = 1
  expected["sub_alias_rn"] = 1
  expected["rsb_alias_rm"] = 1
  expected["eor"] = 1
  expected["and"] = 1
  expected["orr"] = 1
  expected["mov"] = 1
  expected["mvn"] = 1
  expected["bic"] = 1
  expected["bic_alias_rm"] = 1
  expected["add_all_alias"] = 1
  expected["add_sp_lr_alias"] = 1
  expected["mul"] = 1
  expected["mul_alias_rs"] = 1
  expected["mul_sources_alias"] = 1
}

/ command=rv32im-mapped-alu-semantics / {
  backend = field("backend")
  item = field("case")

  if (field("result") != "PASS")
    fail(backend ":" item " reported failure")

  if (item == "all") {
    summary[backend]++
    next
  }
  if (!(item in expected)) {
    fail("unexpected case " item)
    next
  }

  value = field("opcode") ":" field("rd") ":" field("rd_value") ":" \
    field("pc") ":" field("cpsr") ":" field("guest_state_hash")
  observation[backend ":" item] = value
  seen[backend ":" item]++

  if (backend == "interp" &&
      field("harness_mode") != "cpu_cc_interpreter")
    fail(item " did not use the cpu.cc interpreter")
  if (backend == "rv32im") {
    if (field("harness_mode") != "runtime_fixture" ||
        (field("generated_bytes") + 0) <= 0 ||
        (field("native_blocks") + 0) <= 0 ||
        (field("fallbacks") + 0) != 0 ||
        (field("execute_arm_calls") + 0) != 0)
      fail(item " missing generated native-only execution evidence")
  }
}

END {
  for (item in expected) {
    if (seen["interp:" item] != 1)
      fail("expected one interpreter result for " item)
    if (seen["rv32im:" item] != 1)
      fail("expected one RV32IM result for " item)
    if (observation["interp:" item] != observation["rv32im:" item])
      fail(item " interpreter/RV32IM state mismatch")
  }
  if (summary["interp"] != 1 || summary["rv32im"] != 1)
    fail("missing semantic suite summary")
  if (failed)
    exit 1
  print "result=PASS command=rv32im-mapped-alu-parity cases=15 " \
    "operations=and,eor,sub,rsb,add,orr,mov,mvn,bic,mul " \
    "aliases=rd_rn,rd_rm,rn_rm,mul_rd_rs,mul_rm_rs reference=cpu.cc " \
    "reason=interpreter_native_states_equal"
}
