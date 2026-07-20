function value(name,    i, pair) {
  for (i = 1; i <= NF; i++) {
    split($i, pair, "=")
    if (pair[1] == name)
      return pair[2]
  }
  return ""
}

value("command") == "bios-swi" {
  case_name = value("case")
  backend = value("backend")

  if (case_name == "all") {
    if (value("result") != "PASS") {
      printf("BIOS SWI aggregate failed: backend=%s\n", backend)
      failed = 1
    }
    aggregate[backend]++
    next
  }

  if (backend != "interp" && backend != "rv32im") {
    printf("BIOS SWI unknown backend: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
    next
  }
  if (value("result") != "PASS") {
    printf("BIOS SWI case failed: backend=%s case=%s\n", backend, case_name)
    failed = 1
  }
  if (seen[backend, case_name]++) {
    printf("BIOS SWI duplicate case: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
  }
  if (backend == "rv32im" &&
      (value("native_blocks") + 0 < 1 ||
       value("bios_native_blocks") + 0 < 1 ||
       value("bios_blocks_emitted") + 0 < 1 ||
       value("code_bytes") + 0 < 1 ||
       value("fallbacks") != "0" || value("bios_fallbacks") != "0" ||
       value("execute_arm_calls") != "0")) {
    printf("BIOS SWI native proof missing: case=%s\n", case_name)
    failed = 1
  }

  state_hash[backend, case_name] = value("state_hash")
  memory_hash[backend, case_name] = value("memory_hash")
  swi[backend, case_name] = value("swi")
  cases[case_name] = 1
}

END {
  case_count = 0
  for (case_name in cases) {
    case_count++
    if (seen["interp", case_name] != 1 || seen["rv32im", case_name] != 1) {
      printf("BIOS SWI backend missing: case=%s interp=%u rv32im=%u\n",
             case_name, seen["interp", case_name], seen["rv32im", case_name])
      failed = 1
    } else if (swi["interp", case_name] != swi["rv32im", case_name] ||
               state_hash["interp", case_name] != state_hash["rv32im", case_name] ||
               memory_hash["interp", case_name] != memory_hash["rv32im", case_name]) {
      printf("BIOS SWI mismatch: case=%s interp_state=%s rv32im_state=%s interp_memory=%s rv32im_memory=%s\n",
             case_name, state_hash["interp", case_name],
             state_hash["rv32im", case_name], memory_hash["interp", case_name],
             memory_hash["rv32im", case_name])
      failed = 1
    }
  }

  if (case_count != expected_cases) {
    printf("BIOS SWI case count mismatch: got=%u expected=%u\n",
           case_count, expected_cases)
    failed = 1
  }
  if (aggregate["interp"] != 1 || aggregate["rv32im"] != 1) {
    printf("BIOS SWI aggregate missing: interp=%u rv32im=%u\n",
           aggregate["interp"], aggregate["rv32im"])
    failed = 1
  }
  if (failed)
    exit 1

  printf("result=PASS command=bios-swi-compare cases=%u backends=interp,rv32im bios_jit=required fallbacks=0 reason=architectural_state_and_memory_equal\n",
         case_count)
}
