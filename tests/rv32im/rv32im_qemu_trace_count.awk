function normalize_hex(value) {
  value = tolower(value)
  sub(/^0x/, "", value)
  sub(/^0+/, "", value)
  return value == "" ? "0" : value
}

BEGIN {
  if (!expected_windows)
    expected_windows = 14
  begin_pc = normalize_hex(begin_pc)
  end_pc = normalize_hex(end_pc)
  collecting = 0
  active = 0
  windows = 0
}

/^IN: / {
  collecting = 1
  section_insns = 0
  next
}

collecting && /^0x[0-9a-fA-F]+:/ {
  section_insns++
  next
}

/^Trace / {
  tb = $3
  if (collecting) {
    tb_insns[tb] = section_insns
    collecting = 0
  }

  trace = $0
  sub(/^.*\[/, "", trace)
  split(trace, parts, "/")
  pc = normalize_hex(parts[2])

  if (pc == begin_pc) {
    if (active) {
      print "nested qemu trace measurement window" > "/dev/stderr"
      failed = 1
    }
    active = 1
    window_insns = 0
    next
  }

  if (pc == end_pc) {
    if (!active) {
      print "unmatched qemu trace measurement end" > "/dev/stderr"
      failed = 1
      next
    }
    windows++
    print "window=" windows " qemu_trace_raw=" window_insns
    active = 0
    next
  }

  if (active) {
    if (!(tb in tb_insns)) {
      print "missing TB instruction count for " tb > "/dev/stderr"
      failed = 1
    } else {
      window_insns += tb_insns[tb]
    }
  }
}

END {
  if (active) {
    print "unterminated qemu trace measurement window" > "/dev/stderr"
    failed = 1
  }
  if (windows != expected_windows) {
    print "expected " expected_windows " qemu trace windows, got " windows > "/dev/stderr"
    failed = 1
  }
  if (failed)
    exit 1
}
