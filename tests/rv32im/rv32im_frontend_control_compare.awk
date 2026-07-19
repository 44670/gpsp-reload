function value(name,    i, pair) {
  for (i = 1; i <= NF; i++) {
    split($i, pair, "=")
    if (pair[1] == name)
      return pair[2]
  }
  return ""
}

value("command") == "frontend-control" {
  case_name = value("case")
  backend = value("backend")

  if (case_name == "all") {
    if (value("result") != "PASS") {
      printf("frontend control aggregate failed: backend=%s\n", backend)
      failed = 1
    }
    aggregate[backend]++
    next
  }

  if (value("result") != "PASS") {
    printf("frontend control case failed: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
  }
  if (backend != "interp" && backend != "rv32im" &&
      backend != "rv32im-dispatch") {
    printf("frontend control unknown backend: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
    next
  }
  if (seen[backend, case_name]++) {
    printf("frontend control duplicate case: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
  }
  if (value("update_exhausted") != "1") {
    printf("frontend control scheduler boundary missing: backend=%s case=%s\n",
           backend, case_name)
    failed = 1
  }
  if (backend != "interp" &&
      (value("native_blocks") + 0 < 1 || value("code_bytes") + 0 < 1 ||
       value("fallbacks") != "0" || value("execute_arm_calls") != "0")) {
    printf("frontend control native proof missing: case=%s\n", case_name)
    failed = 1
  }
  state_hash[backend, case_name] = value("state_hash")
  generated_words[backend, case_name] = value("generated_words")
  trace_count[backend, case_name] = value("trace_count") + 0
  cases[case_name] = 1
}

END {
  case_count = 0
  for (case_name in cases) {
    case_count++
    if (seen["interp", case_name] != 1 ||
        seen["rv32im", case_name] != 1 ||
        seen["rv32im-dispatch", case_name] != 1) {
      printf("frontend control missing backend: case=%s interp=%u rv32im=%u dispatch=%u\n",
             case_name, seen["interp", case_name],
             seen["rv32im", case_name],
             seen["rv32im-dispatch", case_name])
      failed = 1
    } else if (state_hash["interp", case_name] != state_hash["rv32im", case_name] ||
               state_hash["interp", case_name] != state_hash["rv32im-dispatch", case_name]) {
      printf("frontend control state mismatch: case=%s interp=%s rv32im=%s dispatch=%s\n",
             case_name, state_hash["interp", case_name],
             state_hash["rv32im", case_name],
             state_hash["rv32im-dispatch", case_name])
      failed = 1
    }
    if (generated_words["interp", case_name] != generated_words["rv32im", case_name]) {
      printf("frontend control generated span mismatch: case=%s\n", case_name)
      failed = 1
    }
    if (trace_count["rv32im-dispatch", case_name] > trace_count["rv32im", case_name])
      deopt_cases++
  }

  if (case_count != expected_cases) {
    printf("frontend control case count mismatch: got=%u expected=%u\n",
           case_count, expected_cases)
    failed = 1
  }
  if (aggregate["interp"] != 1 || aggregate["rv32im"] != 1 ||
      aggregate["rv32im-dispatch"] != 1) {
    printf("frontend control aggregate missing: interp=%u rv32im=%u dispatch=%u\n",
           aggregate["interp"], aggregate["rv32im"],
           aggregate["rv32im-dispatch"])
    failed = 1
  }
  if (deopt_cases < 1) {
    printf("frontend control forced dispatcher did not deoptimize any chain\n")
    failed = 1
  }

  if (failed)
    exit 1

  printf("result=PASS command=frontend-control-compare cases=%u deopt_cases=%u " \
         "backends=interp,rv32im,rv32im-dispatch " \
         "isa=rv32imc reason=architectural_state_and_chain_deopt_equal\n",
         case_count, deopt_cases)
}
