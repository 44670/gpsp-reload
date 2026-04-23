"use strict";

const {
  REG_SP,
  REG_LR,
  REG_PC,
  REG_CPSR,
  CPU_MODE,
  CPU_HALT_STATE,
  REG_BUS_VALUE,
  MODE_USER,
  MODE_SYSTEM,
  MODE_SUPERVISOR,
  CPU_ACTIVE,
  CPU_ALERT_HALT,
  CPU_ALERT_SMC,
  CPU_ALERT_IRQ,
  THUMB_BIT,
  CPSR_MASKS,
  SPSR_MASKS,
  popcount16,
} = require("./jsjit_common.js");

const ALERT_MASK = (CPU_ALERT_HALT | CPU_ALERT_IRQ | CPU_ALERT_SMC) >>> 0;

function createEmitterContext() {
  return {
    tempId: 0,
  };
}

function allocTemp(ctx, prefix) {
  const name = `${prefix}${ctx.tempId}`;
  ctx.tempId++;
  return name;
}

function pushSyncAndReturn(lines, valueExpr) {
  lines.push("  runtime.cyclesRemaining = cyclesRemaining | 0;");
  lines.push("  runtime.pendingAlert = (runtime.pendingAlert | pendingAlert) >>> 0;");
  lines.push(`  return ${valueExpr};`);
}

function pushCycleCharge(lines, tableName, addressExpr, wordSized) {
  const idx = wordSized ? 1 : 0;
  lines.push(`  if (((${addressExpr}) >>> 0) < 0x10000000) cyclesRemaining -= ${tableName}[(((((${addressExpr}) >>> 0) >>> 24) << 1) | ${idx})] >>> 0;`);
}

function pushArmComplete(lines, { forceDispatch, skipSeq }) {
  if (!skipSeq) {
    pushCycleCharge(lines, "wsSeq", "state[15]", true);
  }
  lines.push("  if ((idleLoopTargetPc[0] >>> 0) === (state[15] >>> 0) && cyclesRemaining > 0) cyclesRemaining = 0;");
  if (forceDispatch) {
    pushSyncAndReturn(lines, "1");
    return;
  }
  lines.push(`  if (cyclesRemaining <= 0 || (state[${CPU_HALT_STATE}] >>> 0) !== ${CPU_ACTIVE} || (pendingAlert & ${ALERT_MASK}) !== 0 || (state[${REG_CPSR}] & ${THUMB_BIT}) !== 0) {`);
  pushSyncAndReturn(lines, "1");
  lines.push("  }");
}

function pushArmPcDispatch(lines, thumbExpr) {
  lines.push(`  if (${thumbExpr}) {`);
  pushArmComplete(lines, { forceDispatch: true, skipSeq: true });
  lines.push("  }");
  pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
}

function withCondition(lines, cond, nextPc, emitBody) {
  if (cond === 0x0e) {
    emitBody();
    return;
  }

  lines.push(`  if (condPassed(state[${REG_CPSR}] >>> 0, ${cond})) {`);
  emitBody();
  lines.push("  } else {");
  lines.push(`    state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
  lines.push("  }");
}

function armRegExpr(regIndex, pc, kind) {
  if (regIndex === REG_PC) {
    return kind === "shiftreg" ? `${(pc + 12) >>> 0}` : `${(pc + 8) >>> 0}`;
  }
  return `(state[${regIndex}] >>> 0)`;
}

function emitArmOperand2(lines, ctx, pc, opcode) {
  const carryIn = "(state[16] >>> 29) & 1";

  if ((opcode >>> 25) & 1) {
    const imm8 = opcode & 0xff;
    const rotate = ((opcode >>> 8) & 0xf) * 2;
    if (rotate === 0) {
      return {
        valueExpr: `${imm8 >>> 0}`,
        carryExpr: carryIn,
        prelude: [],
      };
    }

    const sh = allocTemp(ctx, "sh");
    const prelude = [`  const ${sh} = shiftImm(${imm8 >>> 0}, 3, ${rotate}, ${carryIn});`];
    return {
      valueExpr: `${sh}.value >>> 0`,
      carryExpr: `${sh}.carry`,
      prelude,
    };
  }

  const rm = opcode & 0xf;
  const shiftType = (opcode >>> 5) & 0x3;
  const valueExpr = armRegExpr(rm, pc, (opcode & 0x10) ? "shiftreg" : "shift");
  const sh = allocTemp(ctx, "sh");
  const prelude = [];

  if (opcode & 0x10) {
    const rs = (opcode >>> 8) & 0xf;
    prelude.push(
      `  const ${sh} = shiftReg(${valueExpr}, ${shiftType}, ${armRegExpr(rs, pc, "shiftreg")}, ${carryIn});`
    );
  } else {
    prelude.push(
      `  const ${sh} = shiftImm(${valueExpr}, ${shiftType}, ${(opcode >>> 7) & 0x1f}, ${carryIn});`
    );
  }

  return {
    valueExpr: `${sh}.value >>> 0`,
    carryExpr: `${sh}.carry`,
    prelude,
  };
}

function emitArmTransferOffset(lines, ctx, pc, opcode) {
  if (((opcode >>> 25) & 1) === 0) {
    return {
      valueExpr: `${opcode & 0x0fff}`,
      prelude: [],
    };
  }

  const rm = opcode & 0xf;
  const shiftType = (opcode >>> 5) & 0x3;
  const sh = allocTemp(ctx, "off");
  const prelude = [
    `  const ${sh} = shiftImm(${armRegExpr(rm, pc, "addr")}, ${shiftType}, ${(opcode >>> 7) & 0x1f}, (state[${REG_CPSR}] >>> 29) & 1);`,
  ];
  return {
    valueExpr: `${sh}.value >>> 0`,
    prelude,
  };
}

function emitArmDataProc(lines, ctx, pc, opcode) {
  const nextPc = (pc + 4) >>> 0;
  const op = (opcode >>> 21) & 0xf;
  const setFlags = (opcode >>> 20) & 1;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;
  const lhsExpr = armRegExpr(rn, pc, "alu");
  const operand2 = emitArmOperand2(lines, ctx, pc, opcode);
  const result = allocTemp(ctx, "res");

  withCondition(lines, opcode >>> 28, nextPc, () => {
    lines.push(...operand2.prelude);

    switch (op) {
      case 0x0:
        lines.push(`  const ${result} = ((${lhsExpr}) & (${operand2.valueExpr})) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      case 0x1:
        lines.push(`  const ${result} = ((${lhsExpr}) ^ (${operand2.valueExpr})) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      case 0x2:
      case 0x3:
      case 0x4:
      case 0x5:
      case 0x6:
      case 0x7:
      case 0xa:
      case 0xb: {
        const add = allocTemp(ctx, "add");
        const aExpr =
          op === 0x3 || op === 0x7
            ? operand2.valueExpr
            : lhsExpr;
        const bExpr =
          op === 0x2 || op === 0xa
            ? `(~(${operand2.valueExpr})) >>> 0`
            : op === 0x3
              ? `(~(${lhsExpr})) >>> 0`
              : op === 0x5
                ? operand2.valueExpr
                : op === 0x6
                  ? `(~(${operand2.valueExpr})) >>> 0`
                  : op === 0x7
                    ? `(~(${lhsExpr})) >>> 0`
                    : operand2.valueExpr;
        const carryExpr =
          op === 0x5 || op === 0x6 || op === 0x7
            ? "(state[16] >>> 29) & 1"
            : op === 0x2 || op === 0x3 || op === 0xa
              ? "1"
              : "0";
        lines.push(`  const ${add} = addWithCarry(${aExpr}, ${bExpr}, ${carryExpr});`);
        if (op === 0xa || op === 0xb) {
          lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${add}.result >>> 0, ${add}.carryOut, ${add}.overflow);`);
          lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
          pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
          return;
        }
        lines.push(`  const ${result} = ${add}.result >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${result}, ${add}.carryOut, ${add}.overflow);`);
        }
        break;
      }
      case 0x8:
        lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ((${lhsExpr}) & (${operand2.valueExpr})) >>> 0, ${operand2.carryExpr});`);
        lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
        pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
        return;
      case 0x9:
        lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ((${lhsExpr}) ^ (${operand2.valueExpr})) >>> 0, ${operand2.carryExpr});`);
        lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
        pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
        return;
      case 0xc:
        lines.push(`  const ${result} = ((${lhsExpr}) | (${operand2.valueExpr})) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      case 0xd:
        lines.push(`  const ${result} = (${operand2.valueExpr}) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      case 0xe:
        lines.push(`  const ${result} = ((${lhsExpr}) & (~(${operand2.valueExpr}) >>> 0)) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      case 0xf:
        lines.push(`  const ${result} = (~(${operand2.valueExpr})) >>> 0;`);
        if (setFlags) {
          lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${result}, ${operand2.carryExpr});`);
        }
        break;
      default:
        throw new Error(`unsupported ARM data-proc op ${op.toString(16)}`);
    }

    if (rd === REG_PC) {
      lines.push(`  state[${REG_PC}] = ${result} >>> 0;`);
      if (setFlags) {
        lines.push("  runtime.cyclesRemaining = cyclesRemaining | 0;");
        lines.push("  runtime.pendingAlert = (runtime.pendingAlert | pendingAlert) >>> 0;");
        lines.push("  runtime.restoreSpsrToCpsr();");
        lines.push("  cyclesRemaining = runtime.cyclesRemaining | 0;");
        lines.push("  pendingAlert = runtime.pendingAlert >>> 0;");
        pushArmPcDispatch(lines, `(state[${REG_CPSR}] & ${THUMB_BIT}) !== 0`);
      } else {
        pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      }
      return;
    }

    lines.push(`  state[${rd}] = ${result} >>> 0;`);
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
  });
}

function emitArmBranch(lines, pc, opcode) {
  const cond = opcode >>> 28;
  const link = ((opcode >>> 24) & 1) !== 0;
  let offset = opcode & 0x00ffffff;
  if (offset & 0x00800000) {
    offset |= 0xff000000;
  }
  const target = (pc + 8 + ((offset << 2) >> 0)) >>> 0;

  withCondition(lines, cond, (pc + 4) >>> 0, () => {
    if (link) {
      lines.push(`  state[${REG_LR}] = ${(pc + 4) >>> 0} >>> 0;`);
    }
    lines.push(`  state[${REG_PC}] = ${target} >>> 0;`);
    pushCycleCharge(lines, "wsNseq", "state[15]", true);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmBx(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const rm = opcode & 0xf;
  const targetExpr = armRegExpr(rm, pc, "bx");

  withCondition(lines, cond, nextPc, () => {
    const target = allocTemp(ctx, "bx");
    lines.push(`  const ${target} = ${targetExpr};`);
    lines.push(`  if (${target} & 1) {`);
    lines.push(`    state[${REG_PC}] = ((${target} - 1) >>> 0);`);
    lines.push(`    state[${REG_CPSR}] |= ${THUMB_BIT};`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: true });
    lines.push("  }");
    lines.push(`  state[${REG_PC}] = ${target} >>> 0;`);
    pushCycleCharge(lines, "wsNseq", target, true);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmSwi(lines, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;

  withCondition(lines, cond, nextPc, () => {
    lines.push(`  state[${REG_BUS_VALUE}] = 0xe3a02004 >>> 0;`);
    lines.push(`  regMode[${((MODE_SUPERVISOR & 0x0f) * 7 + 6) >>> 0}] = ${nextPc} >>> 0;`);
    lines.push(`  spsr[${MODE_SUPERVISOR & 0x0f}] = state[${REG_CPSR}] >>> 0;`);
    lines.push(`  state[${REG_PC}] = 0x00000008;`);
    lines.push(`  runtime.applyCpsr(((state[${REG_CPSR}] & ~0x3f) | 0x13 | 0x80) >>> 0, false);`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmPsr(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const top = (opcode >>> 20) & 0xff;
  const pfield = ((opcode >>> 16) & 1) | ((opcode >>> 18) & 2);

  withCondition(lines, cond, nextPc, () => {
    if (top === 0x10 || top === 0x14) {
      const rd = (opcode >>> 12) & 0xf;
      lines.push(`  state[${rd}] = ${top === 0x14 ? `spsr[state[${CPU_MODE}] & 0x0f] >>> 0` : `state[${REG_CPSR}] >>> 0`};`);
      lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
      pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }

    const source = allocTemp(ctx, "psr");
    if (top === 0x32 || top === 0x36) {
      const rot = ((opcode >>> 8) & 0x0f) * 2;
      if (rot === 0) {
        lines.push(`  const ${source} = ${(opcode & 0xff) >>> 0};`);
      } else {
        lines.push(`  const ${source} = ror32(${(opcode & 0xff) >>> 0}, ${rot}) >>> 0;`);
      }
    } else {
      lines.push(`  const ${source} = state[${opcode & 0x0f}] >>> 0;`);
    }

    if (top === 0x12 || top === 0x32) {
      const userMask = CPSR_MASKS[pfield][0] >>> 0;
      const privMask = CPSR_MASKS[pfield][1] >>> 0;
      const mask = allocTemp(ctx, "psrMask");
      lines.push(`  const ${mask} = (((state[${CPU_MODE}] >>> 4) & 1) !== 0) ? ${privMask} : ${userMask};`);
      lines.push(`  runtime.applyCpsr(((${source} & ${mask}) | (state[${REG_CPSR}] & (~(${mask}) >>> 0))) >>> 0, ((${mask} & 0xff) !== 0));`);
    } else {
      lines.push(`  if ((state[${CPU_MODE}] >>> 0) !== ${MODE_USER} && (state[${CPU_MODE}] >>> 0) !== ${MODE_SYSTEM}) {`);
      lines.push(`    const psrSlot${ctx.tempId} = state[${CPU_MODE}] & 0x0f;`);
      lines.push(`    spsr[psrSlot${ctx.tempId}] = ((${source} & ${SPSR_MASKS[pfield] >>> 0}) | (spsr[psrSlot${ctx.tempId}] & ${(~(SPSR_MASKS[pfield] >>> 0)) >>> 0})) >>> 0;`);
      ctx.tempId++;
      lines.push("  }");
    }

    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmMultiply(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const accumulate = ((opcode >>> 21) & 1) !== 0;
  const setFlags = ((opcode >>> 20) & 1) !== 0;

  withCondition(lines, cond, nextPc, () => {
    if ((opcode & 0x00800000) === 0) {
      const rd = (opcode >>> 16) & 0xf;
      const rn = (opcode >>> 12) & 0xf;
      const rs = (opcode >>> 8) & 0xf;
      const rm = opcode & 0xf;
      const result = allocTemp(ctx, "mul");
      lines.push(`  let ${result} = Math.imul(state[${rm}] | 0, state[${rs}] | 0) >>> 0;`);
      if (accumulate) {
        lines.push(`  ${result} = ((${result} + (state[${rn}] >>> 0)) >>> 0);`);
      }
      lines.push(`  state[${rd}] = ${result} >>> 0;`);
      if (setFlags) {
        lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, ${result} >>> 0);`);
      }
      lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
      pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }

    const signed = ((opcode >>> 22) & 1) !== 0;
    const rdHi = (opcode >>> 16) & 0xf;
    const rdLo = (opcode >>> 12) & 0xf;
    const rs = (opcode >>> 8) & 0xf;
    const rm = opcode & 0xf;
    const a = allocTemp(ctx, "mulA");
    const b = allocTemp(ctx, "mulB");
    const value = allocTemp(ctx, "mulV");
    const current = allocTemp(ctx, "mulC");
    const unsigned64 = allocTemp(ctx, "mulU");
    const lo = allocTemp(ctx, "mulLo");
    const hi = allocTemp(ctx, "mulHi");

    lines.push(`  const ${a} = ${signed ? `BigInt.asIntN(32, BigInt(state[${rm}] >>> 0))` : `BigInt(state[${rm}] >>> 0)`};`);
    lines.push(`  const ${b} = ${signed ? `BigInt.asIntN(32, BigInt(state[${rs}] >>> 0))` : `BigInt(state[${rs}] >>> 0)`};`);
    lines.push(`  let ${value} = ${a} * ${b};`);
    if (accumulate) {
      lines.push(`  const ${current} = (BigInt(state[${rdHi}] >>> 0) << 32n) | BigInt(state[${rdLo}] >>> 0);`);
      lines.push(`  ${value} += ${signed ? `BigInt.asIntN(64, ${current})` : current};`);
    }
    lines.push(`  const ${unsigned64} = BigInt.asUintN(64, ${value});`);
    lines.push(`  const ${lo} = Number(${unsigned64} & 0xffffffffn) >>> 0;`);
    lines.push(`  const ${hi} = Number((${unsigned64} >> 32n) & 0xffffffffn) >>> 0;`);
    lines.push(`  state[${rdLo}] = ${lo};`);
    lines.push(`  state[${rdHi}] = ${hi};`);
    if (setFlags) {
      const cpsr = allocTemp(ctx, "cpsr");
      lines.push(`  let ${cpsr} = state[${REG_CPSR}] & 0x3fffffff;`);
      lines.push(`  if (${hi} & 0x80000000) ${cpsr} |= 0x80000000;`);
      lines.push(`  if ((((${lo}) | (${hi})) >>> 0) === 0) ${cpsr} |= 0x40000000;`);
      lines.push(`  state[${REG_CPSR}] = ${cpsr} >>> 0;`);
    }
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmSwap(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const byte = (opcode & 0x00400000) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;
  const rm = opcode & 0xf;
  const address = allocTemp(ctx, "swpAddr");
  const value = allocTemp(ctx, "swpVal");

  withCondition(lines, cond, nextPc, () => {
    lines.push(`  const ${address} = state[${rn}] >>> 0;`);
    if (byte) {
      lines.push(`  const ${value} = read8(${address} >>> 0) & 0xff;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      lines.push(`  pendingAlert |= write8(${address} >>> 0, state[${rm}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, false);
    } else {
      lines.push(`  const ${value} = read32(${address} >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, true);
      lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${rm}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, true);
    }
    lines.push(`  state[${rd}] = ${value} >>> 0;`);
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmHalfTransfer(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const pre = ((opcode >>> 24) & 1) !== 0;
  const up = ((opcode >>> 23) & 1) !== 0;
  const immediate = ((opcode >>> 22) & 1) !== 0;
  const writeBack = ((opcode >>> 21) & 1) !== 0;
  const load = ((opcode >>> 20) & 1) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;
  const sh = (opcode >>> 5) & 0x3;
  const rawOffsetExpr = immediate
    ? `${((((opcode >>> 4) & 0xf0) | (opcode & 0x0f)) >>> 0)}`
    : armRegExpr(opcode & 0x0f, pc, "addr");

  withCondition(lines, cond, nextPc, () => {
    const base = allocTemp(ctx, "halfBase");
    const offset = allocTemp(ctx, "halfOff");
    const address = allocTemp(ctx, "halfAddr");
    const writeBackValue = allocTemp(ctx, "halfWb");

    lines.push(`  const ${base} = ${armRegExpr(rn, pc, "addr")};`);
    lines.push(`  const ${offset} = ${up ? rawOffsetExpr : `((-(${rawOffsetExpr})) >>> 0)`};`);
    lines.push(`  const ${address} = ${pre ? `((${base} + ${offset}) >>> 0)` : base};`);
    lines.push(`  const ${writeBackValue} = ${pre ? address : `((${base} + ${offset}) >>> 0)`};`);

    if (load) {
      const value = allocTemp(ctx, "halfVal");
      if (sh === 1) {
        lines.push(`  const ${value} = read16(${address} >>> 0) & 0xffff;`);
      } else if (sh === 2) {
        lines.push(`  const ${value} = signExtend(read8(${address} >>> 0) & 0xff, 8) >>> 0;`);
      } else if (sh === 3) {
        lines.push(`  const ${value} = ((${address} >>> 0) & 1) ? (signExtend(read8(${address} >>> 0) & 0xff, 8) >>> 0) : (signExtend(read16(${address} >>> 0) & 0xffff, 16) >>> 0);`);
      } else {
        throw new Error(`unsupported ARM halfword load variant 0x${opcode.toString(16)}`);
      }
      pushCycleCharge(lines, "wsNseq", address, false);
      if (rd === REG_PC) {
        lines.push(`  state[${REG_PC}] = ${value} >>> 0;`);
      } else {
        lines.push(`  state[${rd}] = ${value} >>> 0;`);
      }
    } else {
      if (sh !== 1) {
        throw new Error(`unsupported ARM halfword store variant 0x${opcode.toString(16)}`);
      }
      lines.push(`  pendingAlert |= write16(${address} >>> 0, ${rd === REG_PC ? `${(pc + 12) >>> 0}` : `state[${rd}] >>> 0`}) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, false);
    }

    if (writeBack || !pre) {
      if (rn !== REG_PC) {
        lines.push(`  state[${rn}] = ${writeBackValue} >>> 0;`);
      }
    }

    if (load && rd === REG_PC) {
      pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }

    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function emitArmSingleTransfer(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const pre = ((opcode >>> 24) & 1) !== 0;
  const up = ((opcode >>> 23) & 1) !== 0;
  const byte = ((opcode >>> 22) & 1) !== 0;
  const writeBack = ((opcode >>> 21) & 1) !== 0;
  const load = ((opcode >>> 20) & 1) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;
  const transferOffset = emitArmTransferOffset(lines, ctx, pc, opcode);

  withCondition(lines, cond, nextPc, () => {
    const base = allocTemp(ctx, "xferBase");
    const offset = allocTemp(ctx, "xferOff");
    const address = allocTemp(ctx, "xferAddr");
    const writeBackValue = allocTemp(ctx, "xferWb");

    lines.push(...transferOffset.prelude);
    lines.push(`  const ${base} = ${armRegExpr(rn, pc, "addr")};`);
    lines.push(`  const ${offset} = ${up ? transferOffset.valueExpr : `((-(${transferOffset.valueExpr})) >>> 0)`};`);
    lines.push(`  const ${address} = ${pre ? `((${base} + ${offset}) >>> 0)` : base};`);
    lines.push(`  const ${writeBackValue} = ${pre ? address : `((${base} + ${offset}) >>> 0)`};`);

    if (load) {
      const value = allocTemp(ctx, "xferVal");
      lines.push(`  const ${value} = ${byte ? `read8(${address} >>> 0) & 0xff` : `read32(${address} >>> 0) >>> 0`};`);
      pushCycleCharge(lines, "wsNseq", address, !byte);
      if (rd === REG_PC) {
        lines.push(`  state[${REG_PC}] = ${value} >>> 0;`);
      } else {
        lines.push(`  state[${rd}] = ${value} >>> 0;`);
      }
    } else {
      lines.push(`  pendingAlert |= ${byte ? `write8(${address} >>> 0, ${rd === REG_PC ? `${(pc + 12) >>> 0}` : `state[${rd}] >>> 0`})` : `write32(${address} >>> 0, ${rd === REG_PC ? `${(pc + 12) >>> 0}` : `state[${rd}] >>> 0`})`} >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, !byte);
    }

    if (writeBack || !pre) {
      if (rn !== REG_PC) {
        lines.push(`  state[${rn}] = ${writeBackValue} >>> 0;`);
      }
    }

    if (load && rd === REG_PC) {
      pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }

    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: false, skipSeq: false });
  });
}

function emitArmBlockTransfer(lines, ctx, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const pre = ((opcode >>> 24) & 1) !== 0;
  const up = ((opcode >>> 23) & 1) !== 0;
  const sbit = ((opcode >>> 22) & 1) !== 0;
  const writeBack = ((opcode >>> 21) & 1) !== 0;
  const load = ((opcode >>> 20) & 1) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const regList = opcode & 0xffff;
  const count = popcount16(regList);
  const transferBytes = (count * 4) >>> 0;
  const baseMask = (1 << rn) >>> 0;
  const baseFirst = ((((1 << rn) >>> 0) - 1) & regList) === 0;
  const writeBackFirst = load || !((regList & baseMask) !== 0 && baseFirst);

  withCondition(lines, cond, nextPc, () => {
    const base = allocTemp(ctx, "blkBase");
    const endAddr = allocTemp(ctx, "blkEnd");
    const address = allocTemp(ctx, "blkAddr");
    const oldCpsr = allocTemp(ctx, "blkCpsr");
    const oldMode = allocTemp(ctx, "blkMode");

    lines.push(`  const ${base} = state[${rn}] >>> 0;`);
    lines.push(`  const ${endAddr} = ${up ? `((${base} + ${transferBytes}) >>> 0)` : `((${base} - ${transferBytes}) >>> 0)`};`);
    if (pre) {
      lines.push(`  let ${address} = ${up ? `((${base} + 4) >>> 0)` : endAddr};`);
    } else {
      lines.push(`  let ${address} = ${up ? base : `((${endAddr} + 4) >>> 0)`};`);
    }
    lines.push(`  ${address} &= ~3;`);
    lines.push(`  const ${oldCpsr} = state[${REG_CPSR}] >>> 0;`);
    lines.push(`  const ${oldMode} = state[${CPU_MODE}] >>> 0;`);

    if (sbit && (!load || rn !== REG_PC)) {
      lines.push(`  runtime.setCpuMode(${MODE_USER});`);
    }

    if (writeBack && writeBackFirst) {
      lines.push(`  state[${rn}] = ${endAddr} >>> 0;`);
    }

    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);

    for (let reg = 0; reg < 16; reg++) {
      if (((regList >>> reg) & 1) === 0) {
        continue;
      }

      if (load) {
        lines.push(`  state[${reg}] = read32(${address} >>> 0) >>> 0;`);
      } else {
        lines.push(`  pendingAlert |= write32(${address} >>> 0, ${reg === REG_PC ? `((state[${REG_PC}] + 4) >>> 0)` : `state[${reg}] >>> 0`}) >>> 0;`);
      }
      pushCycleCharge(lines, "wsSeq", address, true);
      lines.push(`  ${address} = ((${address} + 4) >>> 0);`);
    }

    if (writeBack && !writeBackFirst) {
      lines.push(`  state[${rn}] = ${endAddr} >>> 0;`);
    }

    if (sbit && (!load || rn !== REG_PC)) {
      lines.push(`  if (${oldMode} !== (state[${CPU_MODE}] >>> 0)) {`);
      lines.push(`    runtime.setCpuMode(${oldMode});`);
      lines.push("  }");
      lines.push(`  state[${REG_CPSR}] = ${oldCpsr} >>> 0;`);
    }

    if (load && (regList & (1 << REG_PC)) !== 0) {
      if (sbit) {
        lines.push("  runtime.restoreSpsrToCpsr();");
        pushArmPcDispatch(lines, `(state[${REG_CPSR}] & ${THUMB_BIT}) !== 0`);
      } else {
        pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
      }
      return;
    }

    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushArmComplete(lines, { forceDispatch: true, skipSeq: false });
  });
}

function isArmPsrOpcode(opcode) {
  const top = (opcode >>> 20) & 0xff;
  switch (top) {
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16:
    case 0x32:
    case 0x36:
      return true;
    default:
      return false;
  }
}

function emitArmStep(lines, ctx, step) {
  const pc = step.pc >>> 0;
  const opcode = step.opcode >>> 0;

  if ((opcode & 0x0ffffff0) === 0x012fff10) return emitArmBx(lines, ctx, pc, opcode);
  if ((opcode & 0x0f000000) === 0x0f000000) return emitArmSwi(lines, pc, opcode);
  if ((opcode & 0x0e000000) === 0x0a000000) return emitArmBranch(lines, pc, opcode);
  if ((opcode & 0x0e000000) === 0x08000000) return emitArmBlockTransfer(lines, ctx, pc, opcode);
  if ((opcode & 0x0c000000) === 0x04000000) return emitArmSingleTransfer(lines, ctx, pc, opcode);
  if ((opcode & 0x0c000000) === 0x00000000) {
    if ((opcode & 0x02000090) === 0x00000090) {
      if ((opcode & 0x0fc000f0) === 0x00000090 || (opcode & 0x0f8000f0) === 0x00800090) {
        return emitArmMultiply(lines, ctx, pc, opcode);
      }
      if ((opcode & 0x0fb00ff0) === 0x01000090 || (opcode & 0x0fb00ff0) === 0x01400090) {
        return emitArmSwap(lines, ctx, pc, opcode);
      }
      return emitArmHalfTransfer(lines, ctx, pc, opcode);
    }

    if (isArmPsrOpcode(opcode)) {
      return emitArmPsr(lines, ctx, pc, opcode);
    }

    return emitArmDataProc(lines, ctx, pc, opcode);
  }

  throw new Error(`unsupported ARM opcode 0x${opcode.toString(16)} at 0x${pc.toString(16)}`);
}

function buildArmBlockExecutor(block, env) {
  const lines = [
    "\"use strict\";",
    "const runtime = env.runtime;",
    "const read8 = env.read8;",
    "const read16 = env.read16;",
    "const read32 = env.read32;",
    "const write8 = env.write8;",
    "const write16 = env.write16;",
    "const write32 = env.write32;",
    "const signExtend = env.signExtend;",
    "const condPassed = env.condPassed;",
    "const addWithCarry = env.addWithCarry;",
    "const setNZ = env.setNZ;",
    "const setNZC = env.setNZC;",
    "const setNZCV = env.setNZCV;",
    "const shiftImm = env.shiftImm;",
    "const shiftReg = env.shiftReg;",
    "const ror32 = env.ror32;",
    "return function executeBlock(state) {",
    "  const wsSeq = runtime.bridge.wsCycSeq;",
    "  const wsNseq = runtime.bridge.wsCycNseq;",
    "  const idleLoopTargetPc = runtime.bridge.idleLoopTargetPc;",
    "  const regMode = runtime.regMode;",
    "  const spsr = runtime.spsr;",
    "  let cyclesRemaining = runtime.cyclesRemaining | 0;",
    "  let pendingAlert = 0;",
  ];

  if (block.idle) {
    lines.push("  if (cyclesRemaining > 0) cyclesRemaining = 0;");
    pushSyncAndReturn(lines, "1");
    lines.push("};");
    return new Function("env", lines.join("\n"))(env);
  }

  const ctx = createEmitterContext();
  for (const step of block.steps) {
    emitArmStep(lines, ctx, step);
  }

  pushSyncAndReturn(lines, "1");
  lines.push("};");
  return new Function("env", lines.join("\n"))(env);
}

module.exports = {
  buildArmBlockExecutor,
};
