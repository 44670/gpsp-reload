"use strict";

const {
  REG_SP,
  REG_LR,
  REG_PC,
  REG_CPSR,
  CPU_HALT_STATE,
  REG_BUS_VALUE,
  MODE_SUPERVISOR,
  CPU_ACTIVE,
  CPU_ALERT_HALT,
  CPU_ALERT_SMC,
  CPU_ALERT_IRQ,
  THUMB_BIT,
  popcount16,
} = require("./jsjit_common.js");

const BLOCK_RESULT_CYCLES_MASK = 0x00ffffff;

function pushReturn(lines) {
  lines.push(`  return (((pendingAlert & 0xff) << 24) | (cyclesUsed & ${BLOCK_RESULT_CYCLES_MASK})) >>> 0;`);
}

function pushCycleCharge(lines, tableName, addressExpr, wordSized) {
  const idx = wordSized ? 1 : 0;
  lines.push(`  if (((${addressExpr}) >>> 0) < 0x10000000) cyclesUsed = ((cyclesUsed + (${tableName}[(((((${addressExpr}) >>> 0) >>> 24) << 1) | ${idx})] >>> 0)) & ${BLOCK_RESULT_CYCLES_MASK}) >>> 0;`);
}

function pushThumbComplete(lines, { forceDispatch, skipSeq, checkAlert }) {
  if (!skipSeq) {
    pushCycleCharge(lines, "wsSeq", "state[15]", false);
  }
  if (forceDispatch) {
    pushReturn(lines);
    return;
  }
  if (checkAlert) {
    lines.push("  if ((pendingAlert & 0xff) !== 0) {");
    pushReturn(lines);
    lines.push("  }");
  }
}

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

function buildThumbBlockExecutor(block, env) {
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
    "const applyCpsr = env.applyCpsr;",
    "return function executeBlock(state) {",
    "  const wsSeq = runtime.bridge.wsCycSeq;",
    "  const wsNseq = runtime.bridge.wsCycNseq;",
    "  const regMode = runtime.regMode;",
    "  const spsr = runtime.spsr;",
    "  let cyclesUsed = 0;",
    "  let pendingAlert = 0;",
  ];

  const ctx = createEmitterContext();
  for (const step of block.steps) {
    emitThumbStep(lines, ctx, step);
  }

  pushReturn(lines);
  lines.push("};");
  return new Function("env", lines.join("\n"))(env);
}

function emitThumbShift(lines, ctx, pc, opcode, shiftType) {
  const nextPc = (pc + 2) >>> 0;
  const rd = opcode & 0x07;
  const rs = (opcode >>> 3) & 0x07;
  const imm = (opcode >>> 6) & 0x1f;
  const sh = allocTemp(ctx, "sh");
  lines.push(`  const ${sh} = shiftImm(state[${rs}] >>> 0, ${shiftType}, ${imm}, (state[${REG_CPSR}] >>> 29) & 1);`);
  lines.push(`  state[${rd}] = ${sh}.value >>> 0;`);
  lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${sh}.value >>> 0, ${sh}.carry);`);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbAddSub(lines, ctx, pc, opcode, subtract, immediate) {
  const nextPc = (pc + 2) >>> 0;
  const rd = opcode & 0x07;
  const rs = (opcode >>> 3) & 0x07;
  const rn = (opcode >>> 6) & 0x07;
  const rhsExpr = immediate ? `${rn}` : `(state[${rn}] >>> 0)`;
  const res = allocTemp(ctx, "res");
  lines.push(`  const ${res} = addWithCarry(state[${rs}] >>> 0, ${subtract ? `(~(${rhsExpr})) >>> 0` : rhsExpr}, ${subtract ? 1 : 0});`);
  lines.push(`  state[${rd}] = ${res}.result >>> 0;`);
  lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${res}.result >>> 0, ${res}.carryOut, ${res}.overflow);`);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbImmOp(lines, ctx, pc, opcode, kind) {
  const nextPc = (pc + 2) >>> 0;
  const rd = (opcode >>> 8) & 0x07;
  const imm = opcode & 0xff;
  const res = allocTemp(ctx, "res");

  switch (kind) {
    case "mov":
      lines.push(`  state[${rd}] = ${imm} >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, ${imm} >>> 0);`);
      break;
    case "cmp":
      lines.push(`  const ${res} = addWithCarry(state[${rd}] >>> 0, (~${imm}) >>> 0, 1);`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${res}.result >>> 0, ${res}.carryOut, ${res}.overflow);`);
      break;
    case "add":
      lines.push(`  const ${res} = addWithCarry(state[${rd}] >>> 0, ${imm} >>> 0, 0);`);
      lines.push(`  state[${rd}] = ${res}.result >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${res}.result >>> 0, ${res}.carryOut, ${res}.overflow);`);
      break;
    case "sub":
      lines.push(`  const ${res} = addWithCarry(state[${rd}] >>> 0, (~${imm}) >>> 0, 1);`);
      lines.push(`  state[${rd}] = ${res}.result >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${res}.result >>> 0, ${res}.carryOut, ${res}.overflow);`);
      break;
    default:
      throw new Error(`unsupported thumb imm op ${kind}`);
  }

  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbAlu(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const rd = opcode & 0x07;
  const rs = (opcode >>> 3) & 0x07;
  const op = (opcode >>> 6) & 0x0f;
  const lhs = `state[${rd}] >>> 0`;
  const rhs = `state[${rs}] >>> 0`;
  const tmp = allocTemp(ctx, "tmp");

  switch (op) {
    case 0x0:
      lines.push(`  state[${rd}] = ((${lhs}) & (${rhs})) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    case 0x1:
      lines.push(`  state[${rd}] = ((${lhs}) ^ (${rhs})) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x7: {
      const shiftType = op === 0x2 ? 0 : op === 0x3 ? 1 : op === 0x4 ? 2 : 3;
      lines.push(`  const ${tmp} = shiftReg(${lhs}, ${shiftType}, ${rhs}, (state[${REG_CPSR}] >>> 29) & 1);`);
      lines.push(`  state[${rd}] = ${tmp}.value >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZC(state[${REG_CPSR}] >>> 0, ${tmp}.value >>> 0, ${tmp}.carry);`);
      break;
    }
    case 0x5:
      lines.push(`  const ${tmp} = addWithCarry(${lhs}, ${rhs}, (state[${REG_CPSR}] >>> 29) & 1);`);
      lines.push(`  state[${rd}] = ${tmp}.result >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
      break;
    case 0x6:
      lines.push(`  const ${tmp} = addWithCarry(${lhs}, (~(${rhs})) >>> 0, (state[${REG_CPSR}] >>> 29) & 1);`);
      lines.push(`  state[${rd}] = ${tmp}.result >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
      break;
    case 0x8:
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, ((${lhs}) & (${rhs})) >>> 0);`);
      break;
    case 0x9:
      lines.push(`  const ${tmp} = addWithCarry(0, (~(${rhs})) >>> 0, 1);`);
      lines.push(`  state[${rd}] = ${tmp}.result >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
      break;
    case 0xa:
      lines.push(`  const ${tmp} = addWithCarry(${lhs}, (~(${rhs})) >>> 0, 1);`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
      break;
    case 0xb:
      lines.push(`  const ${tmp} = addWithCarry(${lhs}, ${rhs}, 0);`);
      lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
      break;
    case 0xc:
      lines.push(`  state[${rd}] = ((${lhs}) | (${rhs})) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    case 0xd:
      lines.push(`  state[${rd}] = Math.imul(${lhs} | 0, ${rhs} | 0) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    case 0xe:
      lines.push(`  state[${rd}] = ((${lhs}) & (~(${rhs}) >>> 0)) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    case 0xf:
      lines.push(`  state[${rd}] = (~(${rhs})) >>> 0;`);
      lines.push(`  state[${REG_CPSR}] = setNZ(state[${REG_CPSR}] >>> 0, state[${rd}] >>> 0);`);
      break;
    default:
      throw new Error(`unsupported thumb alu op ${op}`);
  }

  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbHiReg(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const top = (opcode >>> 8) & 0xff;
  const rs = (opcode >>> 3) & 0x0f;
  const rd = (((opcode >>> 4) & 0x08) | (opcode & 0x07)) >>> 0;
  const lhsExpr = rd === REG_PC ? `${(pc + 4) >>> 0}` : `(state[${rd}] >>> 0)`;
  const rhsExpr = rs === REG_PC ? `${(pc + 4) >>> 0}` : `(state[${rs}] >>> 0)`;
  const tmp = allocTemp(ctx, "tmp");

  if (top === 0x44) {
    lines.push(`  const ${tmp} = ((${lhsExpr}) + (${rhsExpr})) >>> 0;`);
    if (rd === REG_PC) {
      lines.push(`  state[${REG_PC}] = (${tmp} & ~1) >>> 0;`);
      pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }
    lines.push(`  state[${rd}] = ${tmp} >>> 0;`);
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
    return;
  }

  if (top === 0x45) {
    lines.push(`  const ${tmp} = addWithCarry(${lhsExpr}, (~(${rhsExpr})) >>> 0, 1);`);
    lines.push(`  state[${REG_CPSR}] = setNZCV(state[${REG_CPSR}] >>> 0, ${tmp}.result >>> 0, ${tmp}.carryOut, ${tmp}.overflow);`);
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
    return;
  }

  if (top === 0x46) {
    if (rd === REG_PC) {
      lines.push(`  state[${REG_PC}] = ((${rhsExpr}) & ~1) >>> 0;`);
      pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
      return;
    }
    lines.push(`  state[${rd}] = (${rhsExpr}) >>> 0;`);
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
    pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
    return;
  }

  lines.push(`  const ${tmp} = (${rhsExpr}) >>> 0;`);
  lines.push(`  if (${tmp} & 1) {`);
  lines.push(`    state[${REG_PC}] = (${tmp} - 1) >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
  lines.push("  }");
  lines.push(`  state[${REG_PC}] = ${tmp} >>> 0;`);
  lines.push(`  state[${REG_CPSR}] &= ~${THUMB_BIT};`);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: true });
}

function emitThumbCondBranch(lines, pc, opcode) {
  const cond = (opcode >>> 8) & 0x0f;
  let offset = opcode & 0xff;
  if (offset & 0x80) {
    offset |= ~0xff;
  }
  const target = (pc + 4 + ((offset << 1) >> 0)) >>> 0;
  const nextPc = (pc + 2) >>> 0;
  lines.push(`  state[${REG_PC}] = condPassed(state[${REG_CPSR}] >>> 0, ${cond}) ? (${target} >>> 0) : (${nextPc} >>> 0);`);
  pushCycleCharge(lines, "wsNseq", "state[15]", false);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
}

function emitThumbBranch(lines, pc, opcode) {
  let offset = opcode & 0x07ff;
  if (offset & 0x0400) {
    offset |= ~0x07ff;
  }
  const target = (pc + 4 + ((offset << 1) >> 0)) >>> 0;
  lines.push(`  state[${REG_PC}] = ${target} >>> 0;`);
  pushCycleCharge(lines, "wsNseq", "state[15]", false);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
}

function emitThumbBlPrefix(lines, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const offset = opcode & 0x07ff;
  const target = (pc + 4 + ((offset << 21) >> 9)) >>> 0;
  lines.push(`  state[${REG_LR}] = ${target} >>> 0;`);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbBlSuffix(lines, ctx, pc, opcode) {
  const nextLr = ((pc + 2) | 1) >>> 0;
  const offset = opcode & 0x07ff;
  const tmp = allocTemp(ctx, "tmp");
  lines.push(`  const ${tmp} = ((state[${REG_LR}] >>> 0) + ${(offset << 1) >>> 0}) >>> 0;`);
  lines.push(`  state[${REG_LR}] = ${nextLr} >>> 0;`);
  lines.push(`  state[${REG_PC}] = ${tmp} >>> 0;`);
  pushCycleCharge(lines, "wsNseq", tmp, false);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
}

function emitThumbPcRelativeLoad(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const rd = (opcode >>> 8) & 0x07;
  const imm = (opcode & 0xff) << 2;
  const address = (((pc & ~2) + 4 + imm) >>> 0);
  const tmp = allocTemp(ctx, "tmp");
  lines.push(`  const ${tmp} = read32(${address} >>> 0) >>> 0;`);
  lines.push(`  state[${rd}] = ${tmp} >>> 0;`);
  pushCycleCharge(lines, "wsNseq", address, true);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbRegOffsetTransfer(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const top = (opcode >>> 8) & 0xff;
  const ro = (opcode >>> 6) & 0x07;
  const rb = (opcode >>> 3) & 0x07;
  const rd = opcode & 0x07;
  const address = allocTemp(ctx, "addr");
  const tmp = allocTemp(ctx, "tmp");
  lines.push(`  const ${address} = ((state[${rb}] >>> 0) + (state[${ro}] >>> 0)) >>> 0;`);

  switch (top & 0xfe) {
    case 0x50:
      lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, true);
      break;
    case 0x52:
      lines.push(`  pendingAlert |= write16(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    case 0x54:
      lines.push(`  pendingAlert |= write8(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    case 0x56:
      lines.push(`  state[${rd}] = signExtend(read8(${address} >>> 0) & 0xff, 8) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    case 0x58:
      lines.push(`  state[${rd}] = read32(${address} >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsNseq", address, true);
      break;
    case 0x5a:
      lines.push(`  state[${rd}] = read16(${address} >>> 0) & 0xffff;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    case 0x5c:
      lines.push(`  state[${rd}] = read8(${address} >>> 0) & 0xff;`);
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    case 0x5e:
      lines.push(`  if ((${address} >>> 0) & 1) {`);
      lines.push(`    state[${rd}] = signExtend(read8(${address} >>> 0) & 0xff, 8) >>> 0;`);
      lines.push("  } else {");
      lines.push(`    const ${tmp} = read16(${address} >>> 0) & 0xffff;`);
      lines.push(`    state[${rd}] = signExtend(${tmp}, 16) >>> 0;`);
      lines.push("  }");
      pushCycleCharge(lines, "wsNseq", address, false);
      break;
    default:
      throw new Error(`unsupported thumb reg-offset transfer 0x${opcode.toString(16)}`);
  }

  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false, checkAlert: (top & 0xfe) <= 0x54 });
}

function emitThumbImmOffsetTransfer(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const top = (opcode >>> 8) & 0xff;
  const imm = (opcode >>> 6) & 0x1f;
  const rb = (opcode >>> 3) & 0x07;
  const rd = opcode & 0x07;
  const address = allocTemp(ctx, "addr");

  if (top <= 0x67) {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${(imm << 2) >>> 0}) >>> 0;`);
    lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
    pushCycleCharge(lines, "wsNseq", address, true);
  } else if (top <= 0x6f) {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${(imm << 2) >>> 0}) >>> 0;`);
    lines.push(`  state[${rd}] = read32(${address} >>> 0) >>> 0;`);
    pushCycleCharge(lines, "wsNseq", address, true);
  } else if (top <= 0x77) {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${imm}) >>> 0;`);
    lines.push(`  pendingAlert |= write8(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
    pushCycleCharge(lines, "wsNseq", address, false);
  } else if (top <= 0x7f) {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${imm}) >>> 0;`);
    lines.push(`  state[${rd}] = read8(${address} >>> 0) & 0xff;`);
    pushCycleCharge(lines, "wsNseq", address, false);
  } else if (top <= 0x87) {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${(imm << 1) >>> 0}) >>> 0;`);
    lines.push(`  pendingAlert |= write16(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
    pushCycleCharge(lines, "wsNseq", address, false);
  } else {
    lines.push(`  const ${address} = ((state[${rb}] >>> 0) + ${(imm << 1) >>> 0}) >>> 0;`);
    lines.push(`  state[${rd}] = read16(${address} >>> 0) & 0xffff;`);
    pushCycleCharge(lines, "wsNseq", address, false);
  }

  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false, checkAlert: top <= 0x87 && (top <= 0x67 || (top >= 0x70 && top <= 0x77) || (top >= 0x80 && top <= 0x87)) });
}

function emitThumbSpRelativeTransfer(lines, ctx, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const top = (opcode >>> 8) & 0xff;
  const rd = (opcode >>> 8) & 0x07;
  const address = allocTemp(ctx, "addr");
  lines.push(`  const ${address} = ((state[${REG_SP}] >>> 0) + ${((opcode & 0xff) << 2) >>> 0}) >>> 0;`);

  if (top <= 0x97) {
    lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${rd}] >>> 0) >>> 0;`);
  } else {
    lines.push(`  state[${rd}] = read32(${address} >>> 0) >>> 0;`);
  }

  pushCycleCharge(lines, "wsNseq", address, true);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false, checkAlert: top <= 0x97 });
}

function emitThumbBlockMem(lines, ctx, pc, load, rn, regList, preDec) {
  const nextPc = (pc + 2) >>> 0;
  const count = popcount16(regList & 0xff) + ((regList & 0xff00) ? 1 : 0);
  const baseMask = (1 << rn) >>> 0;
  const baseFirst = ((((1 << rn) >>> 0) - 1) & regList) === 0;
  const writeBackFirst = load || !((regList & baseMask) !== 0 && baseFirst);
  const base = allocTemp(ctx, "base");
  const endAddr = allocTemp(ctx, "end");
  const address = allocTemp(ctx, "addr");

  lines.push(`  const ${base} = state[${rn}] >>> 0;`);
  lines.push(`  const ${endAddr} = ${preDec ? `((${base} - ${(count * 4) >>> 0}) >>> 0)` : `((${base} + ${(count * 4) >>> 0}) >>> 0)`};`);
  lines.push(`  let ${address} = ${preDec ? `${endAddr}` : `${base}`};`);

  if (writeBackFirst) {
    lines.push(`  state[${rn}] = ${endAddr} >>> 0;`);
  }

  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);

  if (load) {
    for (let reg = 0; reg < 8; reg++) {
      if (((regList >>> reg) & 1) === 0) {
        continue;
      }
      lines.push(`  state[${reg}] = read32(${address} >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsSeq", address, true);
      lines.push(`  ${address} = ((${address} + 4) >>> 0);`);
    }

    if (regList & (1 << REG_PC)) {
      lines.push(`  state[${REG_PC}] = (read32(${address} >>> 0) & ~1) >>> 0;`);
      pushCycleCharge(lines, "wsSeq", address, true);
      lines.push(`  ${address} = ((${address} + 4) >>> 0);`);
    }
  } else {
    for (let reg = 0; reg < 8; reg++) {
      if (((regList >>> reg) & 1) === 0) {
        continue;
      }
      lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${reg}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsSeq", address, true);
      lines.push(`  ${address} = ((${address} + 4) >>> 0);`);
    }

    if (regList & (1 << REG_LR)) {
      lines.push(`  pendingAlert |= write32(${address} >>> 0, state[${REG_LR}] >>> 0) >>> 0;`);
      pushCycleCharge(lines, "wsSeq", address, true);
      lines.push(`  ${address} = ((${address} + 4) >>> 0);`);
    }
  }

  if (!writeBackFirst) {
    lines.push(`  state[${rn}] = ${endAddr} >>> 0;`);
  }

  if (!load || (regList & (1 << REG_PC)) === 0) {
    lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  }

  pushThumbComplete(lines, { forceDispatch: true, skipSeq: false });
}

function emitThumbPushPop(lines, ctx, pc, opcode) {
  const top = (opcode >>> 8) & 0xff;
  const load = top >= 0xbc;
  let regList = opcode & 0xff;
  if (top === 0xb5) {
    regList |= 1 << REG_LR;
  }
  if (top === 0xbd) {
    regList |= 1 << REG_PC;
  }
  emitThumbBlockMem(lines, ctx, pc, load, REG_SP, regList, !load);
}

function emitThumbBlockTransfer(lines, ctx, pc, opcode) {
  const top = (opcode >>> 8) & 0xff;
  const load = top >= 0xc8;
  const rn = (opcode >>> 8) & 0x07;
  emitThumbBlockMem(lines, ctx, pc, load, rn, opcode & 0xff, false);
}

function emitThumbSwi(lines, pc) {
  const returnPc = (pc + 2) >>> 0;
  lines.push(`  state[${REG_BUS_VALUE}] = 0xe3a02004 >>> 0;`);
  lines.push(`  regMode[${((MODE_SUPERVISOR & 0x0f) * 7 + 6) >>> 0}] = ${returnPc} >>> 0;`);
  lines.push(`  spsr[${MODE_SUPERVISOR & 0x0f}] = state[${REG_CPSR}] >>> 0;`);
  lines.push(`  state[${REG_PC}] = 0x00000008;`);
  lines.push(`  applyCpsr(((state[${REG_CPSR}] & ~0x3f) | 0x13 | 0x80) >>> 0, false);`);
  pushThumbComplete(lines, { forceDispatch: true, skipSeq: true });
}

function emitThumbAddPcSp(lines, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const rd = (opcode >>> 8) & 0x07;
  const imm = (opcode & 0xff) << 2;
  const top = (opcode >>> 8) & 0xff;
  const base = top <= 0xa7 ? (((pc & ~2) + 4) >>> 0) : null;
  lines.push(`  state[${rd}] = ((${base === null ? `(state[${REG_SP}] >>> 0)` : base} + ${imm}) >>> 0);`);
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbAdjustSp(lines, pc, opcode) {
  const nextPc = (pc + 2) >>> 0;
  const imm = (opcode & 0x7f) << 2;
  if ((opcode >>> 7) & 1) {
    lines.push(`  state[${REG_SP}] = ((state[${REG_SP}] >>> 0) - ${imm}) >>> 0;`);
  } else {
    lines.push(`  state[${REG_SP}] = ((state[${REG_SP}] >>> 0) + ${imm}) >>> 0;`);
  }
  lines.push(`  state[${REG_PC}] = ${nextPc} >>> 0;`);
  pushThumbComplete(lines, { forceDispatch: false, skipSeq: false });
}

function emitThumbStep(lines, ctx, step) {
  const pc = step.pc >>> 0;
  const opcode = step.opcode & 0xffff;
  const top = (opcode >>> 8) & 0xff;

  if (top <= 0x07) return emitThumbShift(lines, ctx, pc, opcode, 0);
  if (top <= 0x0f) return emitThumbShift(lines, ctx, pc, opcode, 1);
  if (top <= 0x17) return emitThumbShift(lines, ctx, pc, opcode, 2);
  if (top <= 0x19) return emitThumbAddSub(lines, ctx, pc, opcode, false, false);
  if (top <= 0x1b) return emitThumbAddSub(lines, ctx, pc, opcode, true, false);
  if (top <= 0x1d) return emitThumbAddSub(lines, ctx, pc, opcode, false, true);
  if (top <= 0x1f) return emitThumbAddSub(lines, ctx, pc, opcode, true, true);
  if (top <= 0x27) return emitThumbImmOp(lines, ctx, pc, opcode, "mov");
  if (top <= 0x2f) return emitThumbImmOp(lines, ctx, pc, opcode, "cmp");
  if (top <= 0x37) return emitThumbImmOp(lines, ctx, pc, opcode, "add");
  if (top <= 0x3f) return emitThumbImmOp(lines, ctx, pc, opcode, "sub");
  if (top >= 0x40 && top <= 0x43) return emitThumbAlu(lines, ctx, pc, opcode);
  if (top >= 0x44 && top <= 0x47) return emitThumbHiReg(lines, ctx, pc, opcode);
  if (top >= 0x48 && top <= 0x4f) return emitThumbPcRelativeLoad(lines, ctx, pc, opcode);
  if (top >= 0x50 && top <= 0x5f) return emitThumbRegOffsetTransfer(lines, ctx, pc, opcode);
  if (top >= 0x60 && top <= 0x8f) return emitThumbImmOffsetTransfer(lines, ctx, pc, opcode);
  if (top >= 0x90 && top <= 0x9f) return emitThumbSpRelativeTransfer(lines, ctx, pc, opcode);
  if (top >= 0xd0 && top <= 0xdd) return emitThumbCondBranch(lines, pc, opcode);
  if (top === 0xdf) return emitThumbSwi(lines, pc);
  if (top >= 0xe0 && top <= 0xe7) return emitThumbBranch(lines, pc, opcode);
  if (top >= 0xf0 && top <= 0xf7) return emitThumbBlPrefix(lines, pc, opcode);
  if (top >= 0xf8 && top <= 0xff) return emitThumbBlSuffix(lines, ctx, pc, opcode);
  if (top >= 0xa0 && top <= 0xaf) return emitThumbAddPcSp(lines, pc, opcode);
  if (top >= 0xb0 && top <= 0xb3) return emitThumbAdjustSp(lines, pc, opcode);
  if (top === 0xb4 || top === 0xb5 || top === 0xbc || top === 0xbd) return emitThumbPushPop(lines, ctx, pc, opcode);
  if (top >= 0xc0 && top <= 0xcf) return emitThumbBlockTransfer(lines, ctx, pc, opcode);

  throw new Error(`unsupported Thumb opcode 0x${opcode.toString(16)} at 0x${pc.toString(16)}`);
}

module.exports = {
  allocTemp,
  buildThumbBlockExecutor,
  BLOCK_RESULT_CYCLES_MASK,
  popcount16,
  pushCycleCharge,
  pushReturn,
  pushThumbComplete,
  REG_BUS_VALUE,
  REG_CPSR,
  REG_LR,
  REG_PC,
  REG_SP,
  MODE_SUPERVISOR,
};
