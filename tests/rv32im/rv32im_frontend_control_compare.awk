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
  r1[backend, case_name] = value("r1")
  r8[backend, case_name] = value("r8")
  pc[backend, case_name] = value("pc")
  update_cycles[backend, case_name] = value("update_cycles")
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
    } else if (case_name == "arm_conditional_load_skipped_cycle_loop") {
      # The threaded frontend uses a coarser ROM fetch model than cpu.cc, so
      # compare its two native dispatch policies directly.  Two iterations
      # prove the skipped LDR did not leak its taken-only +2 latency.
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          r1["rv32im", case_name] != "0x00000002" ||
          r1["rv32im-dispatch", case_name] != "0x00000002") {
        printf("frontend control skipped conditional-load cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_r1=%s dispatch_r1=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               r1["rv32im", case_name], r1["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "arm_conditional_exit_group_cycle_boundary") {
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          r8["rv32im", case_name] != "0x00000001" ||
          r8["rv32im-dispatch", case_name] != "0x00000001") {
        printf("frontend control conditional-exit cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_r8=%s dispatch_r8=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               r8["rv32im", case_name], r8["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "arm_conditional_exit_group_false_cycles") {
      # cpu.cc and cpu_threaded use different absolute branch costs.  Lock the
      # native model to two independently charged false exit markers: sharing
      # their gate leaves update_cycles at -6 instead of the correct -18.
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          r8["rv32im", case_name] != "0x00000002" ||
          r8["rv32im-dispatch", case_name] != "0x00000002" ||
          update_cycles["rv32im", case_name] != "0xffffffee" ||
          update_cycles["rv32im-dispatch", case_name] != "0xffffffee") {
        printf("frontend control false conditional-exit cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_r8=%s dispatch_r8=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               r8["rv32im", case_name], r8["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "arm_conditional_swap_cycle_boundary") {
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          r8["rv32im", case_name] != "0x00000001" ||
          r8["rv32im-dispatch", case_name] != "0x00000001") {
        printf("frontend control conditional SWP cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_r8=%s dispatch_r8=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               r8["rv32im", case_name], r8["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "thumb_block_tail_cycles") {
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          pc["rv32im", case_name] != "0x08010800" ||
          pc["rv32im-dispatch", case_name] != "0x08010800" ||
          update_cycles["rv32im", case_name] !~ /^0xffff/ ||
          update_cycles["rv32im-dispatch", case_name] !~ /^0xffff/) {
        printf("frontend control Thumb tail cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_pc=%s dispatch_pc=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               pc["rv32im", case_name], pc["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "arm_known_false_block_tail_cycles") {
      # A dynarec scheduler checkpoint is block-granular, so its final PC need
      # not match the interpreter's instruction-granular stop. It must still
      # publish the tail debit instead of reaching the idle check with zero.
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          pc["rv32im", case_name] != "0x08011000" ||
          pc["rv32im-dispatch", case_name] != "0x08011000" ||
          update_cycles["rv32im", case_name] !~ /^0xffff/ ||
          update_cycles["rv32im-dispatch", case_name] !~ /^0xffff/) {
        printf("frontend control folded-tail cycle mismatch: rv32im_state=%s dispatch_state=%s rv32im_pc=%s dispatch_pc=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               pc["rv32im", case_name], pc["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "thumb_pc_pool_conditional_cycle_loop") {
      # cpu.cc and cpu_threaded.c intentionally use different absolute cycle
      # costs. This case locks the native frontend model: the folded LDR is
      # charged and the condition gate is not charged again by its successor.
      if (state_hash["rv32im", case_name] != state_hash["rv32im-dispatch", case_name] ||
          r1["rv32im", case_name] != "0x00000003" ||
          r1["rv32im-dispatch", case_name] != "0x00000003" ||
          update_cycles["rv32im", case_name] != "0xfffffffe" ||
          update_cycles["rv32im-dispatch", case_name] != "0xfffffffe") {
        printf("frontend control Thumb cycle accounting mismatch: rv32im_state=%s dispatch_state=%s rv32im_r1=%s dispatch_r1=%s rv32im_cycles=%s dispatch_cycles=%s\n",
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               r1["rv32im", case_name], r1["rv32im-dispatch", case_name],
               update_cycles["rv32im", case_name],
               update_cycles["rv32im-dispatch", case_name])
        failed = 1
      }
    } else if (case_name == "thumb_empty_stm_does_not_take_store_alert") {
      # No helper ran, so a0 still contains the nonzero block-entry pointer.
      # Testing it as an alert result causes a phantom dispatcher exit before
      # ADD.  Architectural state eventually converges, so lock the execution
      # boundary too: the internal self-loop must stay in the original block.
      if (state_hash["interp", case_name] != state_hash["rv32im", case_name] ||
          state_hash["interp", case_name] != state_hash["rv32im-dispatch", case_name] ||
          trace_count["rv32im", case_name] != 1 ||
          trace_count["rv32im-dispatch", case_name] != 1) {
        printf("frontend control empty STM phantom exit: interp_state=%s rv32im_state=%s dispatch_state=%s rv32im_trace=%u dispatch_trace=%u\n",
               state_hash["interp", case_name],
               state_hash["rv32im", case_name],
               state_hash["rv32im-dispatch", case_name],
               trace_count["rv32im", case_name],
               trace_count["rv32im-dispatch", case_name])
        failed = 1
      }
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
