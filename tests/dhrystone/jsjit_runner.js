#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const DHRY_RESULT_MAGIC = 0x44524859 >>> 0;
const DHRY_STATUS_PASS = 0;

const EWRAM_BASE = 0x02000000 >>> 0;
const EWRAM_SIZE = 256 * 1024;
const IWRAM_BASE = 0x03000000 >>> 0;
const IWRAM_SIZE = 32 * 1024;
const ROM_BASE = 0x08000000 >>> 0;

function parseArgs(argv) {
  const opts = {
    rom: path.join(__dirname, "dhrystone.gba"),
    maxBlocks: 2_000_000,
  };

  for (let i = 2; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--rom" && i + 1 < argv.length) {
      opts.rom = argv[++i];
    } else if (arg === "--max-blocks" && i + 1 < argv.length) {
      opts.maxBlocks = Number(argv[++i]);
    } else {
      throw new Error(`usage: ${argv[1]} [--rom path] [--max-blocks n]`);
    }
  }

  return opts;
}

function readU32LE(buf, offset) {
  return (
    (buf[offset] |
      (buf[offset + 1] << 8) |
      (buf[offset + 2] << 16) |
      (buf[offset + 3] << 24)) >>>
    0
  );
}

class Memory {
  constructor(rom) {
    this.rom = rom;
    this.ewram = new Uint8Array(EWRAM_SIZE);
    this.iwram = new Uint8Array(IWRAM_SIZE);
  }

  fetch32(addr) {
    return this.read32(addr);
  }

  read8(addr) {
    addr >>>= 0;
    if (addr >= EWRAM_BASE && addr < (EWRAM_BASE + EWRAM_SIZE) >>> 0) {
      return this.ewram[addr - EWRAM_BASE] >>> 0;
    }
    if (addr >= IWRAM_BASE && addr < (IWRAM_BASE + IWRAM_SIZE) >>> 0) {
      return this.iwram[addr - IWRAM_BASE] >>> 0;
    }
    if (addr >= ROM_BASE && (addr - ROM_BASE) < this.rom.length) {
      return this.rom[addr - ROM_BASE] >>> 0;
    }
    return 0;
  }

  read16(addr) {
    addr >>>= 0;
    const aligned = addr & ~1;
    const value = this.read8(aligned) | (this.read8(aligned + 1) << 8);
    if ((addr & 1) === 0) {
      return value >>> 0;
    }
    return (((value >>> 8) | ((value & 0xff) << 8)) >>> 0);
  }

  read32(addr) {
    addr >>>= 0;
    const aligned = addr & ~3;
    const value =
      this.read8(aligned) |
      (this.read8(aligned + 1) << 8) |
      (this.read8(aligned + 2) << 16) |
      (this.read8(aligned + 3) << 24);
    const rotate = (addr & 3) * 8;
    if (rotate === 0) {
      return value >>> 0;
    }
    return (((value >>> rotate) | (value << (32 - rotate))) >>> 0);
  }

  write8(addr, value) {
    addr >>>= 0;
    value >>>= 0;
    if (addr >= EWRAM_BASE && addr < (EWRAM_BASE + EWRAM_SIZE) >>> 0) {
      this.ewram[addr - EWRAM_BASE] = value & 0xff;
      return;
    }
    if (addr >= IWRAM_BASE && addr < (IWRAM_BASE + IWRAM_SIZE) >>> 0) {
      this.iwram[addr - IWRAM_BASE] = value & 0xff;
    }
  }

  write16(addr, value) {
    addr >>>= 0;
    value >>>= 0;
    this.write8(addr, value & 0xff);
    this.write8((addr + 1) >>> 0, (value >>> 8) & 0xff);
  }

  write32(addr, value) {
    addr >>>= 0;
    value >>>= 0;
    this.write8(addr, value & 0xff);
    this.write8((addr + 1) >>> 0, (value >>> 8) & 0xff);
    this.write8((addr + 2) >>> 0, (value >>> 16) & 0xff);
    this.write8((addr + 3) >>> 0, (value >>> 24) & 0xff);
  }

  readResult() {
    return {
      magic: this.read32(EWRAM_BASE),
      status: this.read32(EWRAM_BASE + 4),
      iterations: this.read32(EWRAM_BASE + 8),
      intGlob: this.read32(EWRAM_BASE + 12) | 0,
      boolGlob: this.read32(EWRAM_BASE + 16) | 0,
      ch1: this.read32(EWRAM_BASE + 20) & 0xff,
      ch2: this.read32(EWRAM_BASE + 24) & 0xff,
      arr1_8: this.read32(EWRAM_BASE + 28) | 0,
      arr2_8_7: this.read32(EWRAM_BASE + 32) | 0,
      ptrIntComp: this.read32(EWRAM_BASE + 36) | 0,
      nextPtrIntComp: this.read32(EWRAM_BASE + 40) | 0,
    };
  }
}

function condPassed(cpsr, cond) {
  const n = (cpsr >>> 31) & 1;
  const z = (cpsr >>> 30) & 1;
  const c = (cpsr >>> 29) & 1;
  const v = (cpsr >>> 28) & 1;

  switch (cond) {
    case 0x0:
      return z !== 0;
    case 0x1:
      return z === 0;
    case 0x2:
      return c !== 0;
    case 0x3:
      return c === 0;
    case 0x4:
      return n !== 0;
    case 0x5:
      return n === 0;
    case 0x6:
      return v !== 0;
    case 0x7:
      return v === 0;
    case 0x8:
      return c !== 0 && z === 0;
    case 0x9:
      return c === 0 || z !== 0;
    case 0xa:
      return n === v;
    case 0xb:
      return n !== v;
    case 0xc:
      return z === 0 && n === v;
    case 0xd:
      return z !== 0 || n !== v;
    case 0xe:
      return true;
    default:
      return false;
  }
}

function setNZ(cpsr, value) {
  cpsr &= 0x0fffffff;
  if (value & 0x80000000) {
    cpsr |= 0x80000000;
  }
  if ((value >>> 0) === 0) {
    cpsr |= 0x40000000;
  }
  return cpsr >>> 0;
}

function setNZC(cpsr, value, carry) {
  cpsr = setNZ(cpsr, value);
  cpsr &= ~0x20000000;
  if (carry) {
    cpsr |= 0x20000000;
  }
  return cpsr >>> 0;
}

function setNZCV(cpsr, value, carry, overflow) {
  cpsr = setNZC(cpsr, value, carry);
  cpsr &= ~0x10000000;
  if (overflow) {
    cpsr |= 0x10000000;
  }
  return cpsr >>> 0;
}

function addWithCarry(a, b, carryIn) {
  const ua = a >>> 0;
  const ub = b >>> 0;
  const sum = ua + ub + (carryIn >>> 0);
  const result = sum >>> 0;
  const carryOut = sum > 0xffffffff ? 1 : 0;
  const overflow =
    (((ua ^ ub) & 0x80000000) === 0 && ((ua ^ result) & 0x80000000) !== 0)
      ? 1
      : 0;
  return { result, carryOut, overflow };
}

function asr32(value, shift) {
  return (value >> shift) >>> 0;
}

function shiftImm(value, type, amount, carryIn) {
  value >>>= 0;
  switch (type) {
    case 0: {
      if (amount === 0) {
        return { value, carry: carryIn };
      }
      return {
        value: (value << amount) >>> 0,
        carry: (value >>> (32 - amount)) & 1,
      };
    }
    case 1: {
      if (amount === 0) {
        return { value: 0, carry: (value >>> 31) & 1 };
      }
      return {
        value: value >>> amount,
        carry: (value >>> (amount - 1)) & 1,
      };
    }
    case 2: {
      if (amount === 0) {
        const carry = (value >>> 31) & 1;
        return { value: carry ? 0xffffffff : 0, carry };
      }
      return {
        value: asr32(value | 0, amount),
        carry: (value >>> (amount - 1)) & 1,
      };
    }
    case 3: {
      if (amount === 0) {
        const result = ((carryIn << 31) | (value >>> 1)) >>> 0;
        return { value: result, carry: value & 1 };
      }
      const rot = amount & 31;
      if (rot === 0) {
        return { value, carry: (value >>> 31) & 1 };
      }
      return {
        value: ((value >>> rot) | (value << (32 - rot))) >>> 0,
        carry: (value >>> (rot - 1)) & 1,
      };
    }
    default:
      throw new Error(`invalid shift type ${type}`);
  }
}

function shiftReg(value, type, amount, carryIn) {
  value >>>= 0;
  amount &= 0xff;
  if (amount === 0) {
    return { value, carry: carryIn };
  }

  switch (type) {
    case 0:
      if (amount >= 32) {
        return { value: 0, carry: amount === 32 ? value & 1 : 0 };
      }
      return {
        value: (value << amount) >>> 0,
        carry: (value >>> (32 - amount)) & 1,
      };
    case 1:
      if (amount >= 32) {
        return { value: 0, carry: amount === 32 ? (value >>> 31) & 1 : 0 };
      }
      return {
        value: value >>> amount,
        carry: (value >>> (amount - 1)) & 1,
      };
    case 2:
      if (amount >= 32) {
        const carry = (value >>> 31) & 1;
        return { value: carry ? 0xffffffff : 0, carry };
      }
      return {
        value: asr32(value | 0, amount),
        carry: (value >>> (amount - 1)) & 1,
      };
    case 3: {
      const rot = amount & 31;
      if (rot === 0) {
        return { value, carry: (value >>> 31) & 1 };
      }
      return {
        value: ((value >>> rot) | (value << (32 - rot))) >>> 0,
        carry: (value >>> (rot - 1)) & 1,
      };
    }
    default:
      throw new Error(`invalid reg shift type ${type}`);
  }
}

function readReg(state, reg, pc, mode) {
  if (reg === 15) {
    if (mode === "shiftreg") {
      return (pc + 12) >>> 0;
    }
    return (pc + 8) >>> 0;
  }
  return state.regs[reg] >>> 0;
}

function decodeOperand2(state, pc, opcode) {
  const carryIn = (state.cpsr >>> 29) & 1;

  if ((opcode >>> 25) & 1) {
    const imm8 = opcode & 0xff;
    const rotate = ((opcode >>> 8) & 0xf) * 2;
    if (rotate === 0) {
      return { value: imm8 >>> 0, carry: carryIn };
    }
    return shiftImm(imm8 >>> 0, 3, rotate, carryIn);
  }

  const rm = opcode & 0xf;
  const value = readReg(state, rm, pc, "shift");
  const shiftType = (opcode >>> 5) & 0x3;

  if (opcode & 0x10) {
    const rs = (opcode >>> 8) & 0xf;
    const amount = readReg(state, rs, pc, "shiftreg");
    return shiftReg(value, shiftType, amount, carryIn);
  }

  const imm = (opcode >>> 7) & 0x1f;
  return shiftImm(value, shiftType, imm, carryIn);
}

function decodeTransferOffset(state, pc, opcode) {
  if (((opcode >>> 25) & 1) === 0) {
    return opcode & 0xfff;
  }

  if (opcode & 0x10) {
    throw new Error(`register-shifted transfer offset not supported: pc=${hex(pc)} opcode=${hex(opcode)}`);
  }

  const rm = opcode & 0xf;
  const value = readReg(state, rm, pc, "addr");
  const shiftType = (opcode >>> 5) & 0x3;
  const imm = (opcode >>> 7) & 0x1f;
  return shiftImm(value, shiftType, imm, (state.cpsr >>> 29) & 1).value >>> 0;
}

function execDataProc(state, mem, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const op = (opcode >>> 21) & 0xf;
  const setFlags = (opcode >>> 20) & 1;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  const lhs = readReg(state, rn, pc, "alu");
  const sh = decodeOperand2(state, pc, opcode);
  const rhs = sh.value >>> 0;
  let result = 0;

  switch (op) {
    case 0x0:
      result = lhs & rhs;
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    case 0x1:
      result = lhs ^ rhs;
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    case 0x2: {
      const sub = addWithCarry(lhs, (~rhs) >>> 0, 1);
      result = sub.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, sub.carryOut, sub.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x3: {
      const sub = addWithCarry(rhs, (~lhs) >>> 0, 1);
      result = sub.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, sub.carryOut, sub.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x4: {
      const add = addWithCarry(lhs, rhs, 0);
      result = add.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, add.carryOut, add.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x5: {
      const add = addWithCarry(lhs, rhs, (state.cpsr >>> 29) & 1);
      result = add.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, add.carryOut, add.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x6: {
      const sub = addWithCarry(lhs, (~rhs) >>> 0, (state.cpsr >>> 29) & 1);
      result = sub.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, sub.carryOut, sub.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x7: {
      const sub = addWithCarry(rhs, (~lhs) >>> 0, (state.cpsr >>> 29) & 1);
      result = sub.result;
      if (setFlags) state.cpsr = setNZCV(state.cpsr, result, sub.carryOut, sub.overflow);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    }
    case 0x8:
      result = lhs & rhs;
      state.cpsr = setNZC(state.cpsr, result, sh.carry);
      break;
    case 0x9:
      result = lhs ^ rhs;
      state.cpsr = setNZC(state.cpsr, result, sh.carry);
      break;
    case 0xa: {
      const sub = addWithCarry(lhs, (~rhs) >>> 0, 1);
      state.cpsr = setNZCV(state.cpsr, sub.result, sub.carryOut, sub.overflow);
      break;
    }
    case 0xb: {
      const add = addWithCarry(lhs, rhs, 0);
      state.cpsr = setNZCV(state.cpsr, add.result, add.carryOut, add.overflow);
      break;
    }
    case 0xc:
      result = lhs | rhs;
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    case 0xd:
      result = rhs;
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    case 0xe:
      result = lhs & (~rhs);
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    case 0xf:
      result = (~rhs) >>> 0;
      if (setFlags) state.cpsr = setNZC(state.cpsr, result, sh.carry);
      if (rd !== 15) state.regs[rd] = result >>> 0;
      break;
    default:
      throw new Error(`unsupported data proc op ${op.toString(16)}`);
  }

  if (rd === 15 && (op < 0x8 || op >= 0xc)) {
    state.regs[15] = result & ~3;
    return state.regs[15] >>> 0;
  }

  state.regs[15] = nextPc;
  return nextPc;
}

function execSingleTransfer(state, mem, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const pre = ((opcode >>> 24) & 1) !== 0;
  const up = ((opcode >>> 23) & 1) !== 0;
  const byte = ((opcode >>> 22) & 1) !== 0;
  const writeBack = ((opcode >>> 21) & 1) !== 0;
  const load = ((opcode >>> 20) & 1) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const rd = (opcode >>> 12) & 0xf;

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  const base = readReg(state, rn, pc, "addr");
  let offset = decodeTransferOffset(state, pc, opcode);
  if (!up) {
    offset = (-offset) >>> 0;
  }

  let address = pre ? ((base + offset) >>> 0) : base;
  const wbValue = pre ? address : ((base + offset) >>> 0);

  if (load) {
    let value;
    if (byte) {
      value = mem.read8(address);
    } else {
      value = mem.read32(address);
    }
    if (rd === 15) {
      state.regs[15] = value & ~3;
      if (writeBack && rn !== 15) {
        state.regs[rn] = wbValue >>> 0;
      }
      return state.regs[15] >>> 0;
    }
    state.regs[rd] = value >>> 0;
  } else {
    let value = readReg(state, rd, pc, "store");
    if (rd === 15) {
      value = (pc + 12) >>> 0;
    }
    if (byte) {
      mem.write8(address, value);
    } else {
      mem.write32(address, value);
    }
  }

  if (writeBack || !pre) {
    if (rn !== 15) {
      state.regs[rn] = wbValue >>> 0;
    }
  }

  state.regs[15] = nextPc;
  return nextPc;
}

function popcount16(value) {
  value &= 0xffff;
  let count = 0;
  while (value) {
    value &= value - 1;
    count++;
  }
  return count;
}

function execBlockTransfer(state, mem, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;
  const pre = ((opcode >>> 24) & 1) !== 0;
  const up = ((opcode >>> 23) & 1) !== 0;
  const writeBack = ((opcode >>> 21) & 1) !== 0;
  const load = ((opcode >>> 20) & 1) !== 0;
  const rn = (opcode >>> 16) & 0xf;
  const regList = opcode & 0xffff;
  const count = popcount16(regList);

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  let address = state.regs[rn] >>> 0;
  if (up) {
    address = pre ? (address + 4) >>> 0 : address;
  } else {
    address = pre
      ? (address - count * 4) >>> 0
      : (address - (count - 1) * 4) >>> 0;
  }

  for (let reg = 0; reg < 16; reg++) {
    if (((regList >>> reg) & 1) === 0) {
      continue;
    }
    if (load) {
      const value = mem.read32(address);
      if (reg === 15) {
        state.regs[15] = value & ~3;
      } else {
        state.regs[reg] = value >>> 0;
      }
    } else {
      let value = state.regs[reg] >>> 0;
      if (reg === 15) {
        value = (pc + 12) >>> 0;
      }
      mem.write32(address, value);
    }
    address = (address + 4) >>> 0;
  }

  if (writeBack) {
    const base = state.regs[rn] >>> 0;
    state.regs[rn] = up ? (base + count * 4) >>> 0 : (base - count * 4) >>> 0;
  }

  if (!load || ((regList >>> 15) & 1) === 0) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  return state.regs[15] >>> 0;
}

function execMultiply(state, pc, opcode) {
  const cond = opcode >>> 28;
  const nextPc = (pc + 4) >>> 0;

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  const accumulate = ((opcode >>> 21) & 1) !== 0;
  const setFlags = ((opcode >>> 20) & 1) !== 0;

  if ((opcode & 0x00800000) === 0) {
    const rd = (opcode >>> 16) & 0xf;
    const rn = (opcode >>> 12) & 0xf;
    const rs = (opcode >>> 8) & 0xf;
    const rm = opcode & 0xf;
    let result = Math.imul(state.regs[rm] | 0, state.regs[rs] | 0) >>> 0;
    if (accumulate) {
      result = (result + (state.regs[rn] >>> 0)) >>> 0;
    }
    state.regs[rd] = result >>> 0;
    if (setFlags) {
      state.cpsr = setNZ(state.cpsr, result);
    }
    state.regs[15] = nextPc;
    return nextPc;
  }

  const signed = ((opcode >>> 22) & 1) !== 0;
  const rdHi = (opcode >>> 16) & 0xf;
  const rdLo = (opcode >>> 12) & 0xf;
  const rs = (opcode >>> 8) & 0xf;
  const rm = opcode & 0xf;
  const a = signed ? BigInt.asIntN(32, BigInt(state.regs[rm] >>> 0)) : BigInt(state.regs[rm] >>> 0);
  const b = signed ? BigInt.asIntN(32, BigInt(state.regs[rs] >>> 0)) : BigInt(state.regs[rs] >>> 0);
  let result = a * b;

  if (accumulate) {
    const current = (BigInt(state.regs[rdHi] >>> 0) << 32n) | BigInt(state.regs[rdLo] >>> 0);
    result += signed ? BigInt.asIntN(64, current) : current;
  }

  const unsigned64 = BigInt.asUintN(64, result);
  state.regs[rdLo] = Number(unsigned64 & 0xffffffffn) >>> 0;
  state.regs[rdHi] = Number((unsigned64 >> 32n) & 0xffffffffn) >>> 0;
  if (setFlags) {
    state.cpsr = setNZ(state.cpsr, state.regs[rdHi]);
  }
  state.regs[15] = nextPc;
  return nextPc;
}

function execBranch(state, pc, opcode) {
  const cond = opcode >>> 28;
  const link = ((opcode >>> 24) & 1) !== 0;
  const nextPc = (pc + 4) >>> 0;

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  let offset = opcode & 0x00ffffff;
  if (offset & 0x00800000) {
    offset |= 0xff000000;
  }
  offset = (offset << 2) >> 0;
  if (link) {
    state.regs[14] = (pc + 4) >>> 0;
  }
  state.regs[15] = (pc + 8 + offset) >>> 0;
  return state.regs[15] >>> 0;
}

function execBx(state, pc, opcode) {
  const cond = opcode >>> 28;
  const rn = opcode & 0xf;
  const nextPc = (pc + 4) >>> 0;

  if (!condPassed(state.cpsr, cond)) {
    state.regs[15] = nextPc;
    return nextPc;
  }

  const target = readReg(state, rn, pc, "bx");
  if (target & 1) {
    throw new Error(`thumb branch not supported: pc=${hex(pc)} target=${hex(target)}`);
  }
  state.regs[15] = target & ~3;
  return state.regs[15] >>> 0;
}

function hex(value) {
  return `0x${(value >>> 0).toString(16).padStart(8, "0")}`;
}

function classify(opcode) {
  opcode >>>= 0;

  if ((opcode & 0x0ffffff0) === 0x012fff10) {
    return "bx";
  }
  if ((opcode & 0x0e000000) === 0x0a000000) {
    return "branch";
  }
  if ((opcode & 0x0fc000f0) === 0x00000090 || (opcode & 0x0f8000f0) === 0x00800090) {
    return "multiply";
  }
  if ((opcode & 0x0e000000) === 0x08000000) {
    return "block";
  }
  if ((opcode & 0x0c000000) === 0x04000000) {
    return "transfer";
  }
  if ((opcode & 0x0c000000) === 0x00000000) {
    if ((opcode & 0x000000f0) === 0x00000090) {
      return "multiply";
    }
    if ((opcode & 0x01900090) === 0x01000090) {
      return "unsupported";
    }
    return "dataproc";
  }

  return "unsupported";
}

class JitRunner {
  constructor(romPath) {
    this.memory = new Memory(fs.readFileSync(romPath));
    this.state = {
      regs: new Uint32Array(16),
      cpsr: 0x0000001f,
      blocksExecuted: 0,
      steps: 0,
    };
    this.state.regs[13] = 0x03007f00;
    this.state.regs[15] = ROM_BASE;
    this.blockCache = new Map();
    this.translated = 0;
  }

  compileBlock(pc) {
    const lines = [
      "\"use strict\";",
      "return function block(state, mem, helpers) {",
      "  let nextPc = state.regs[15] >>> 0;",
    ];

    let curPc = pc >>> 0;
    for (let count = 0; count < 64; count++) {
      const opcode = this.memory.fetch32(curPc) >>> 0;
      const kind = classify(opcode);
      const seqPc = (curPc + 4) >>> 0;

      lines.push(`  nextPc = ${hex(seqPc)};`);

      switch (kind) {
        case "dataproc":
          lines.push(`  if (helpers.execDataProc(state, mem, ${hex(curPc)}, ${hex(opcode)}) !== ${hex(seqPc)}) return state.regs[15] >>> 0;`);
          break;
        case "transfer":
          lines.push(`  if (helpers.execSingleTransfer(state, mem, ${hex(curPc)}, ${hex(opcode)}) !== ${hex(seqPc)}) return state.regs[15] >>> 0;`);
          break;
        case "block":
          lines.push(`  if (helpers.execBlockTransfer(state, mem, ${hex(curPc)}, ${hex(opcode)}) !== ${hex(seqPc)}) return state.regs[15] >>> 0;`);
          break;
        case "multiply":
          lines.push(`  if (helpers.execMultiply(state, ${hex(curPc)}, ${hex(opcode)}) !== ${hex(seqPc)}) return state.regs[15] >>> 0;`);
          break;
        case "branch":
          lines.push(`  return helpers.execBranch(state, ${hex(curPc)}, ${hex(opcode)});`);
          lines.push("};");
          this.translated++;
          return new Function(lines.join("\n"))();
        case "bx":
          lines.push(`  return helpers.execBx(state, ${hex(curPc)}, ${hex(opcode)});`);
          lines.push("};");
          this.translated++;
          return new Function(lines.join("\n"))();
        default:
          lines.push(`  throw new Error("unsupported opcode at ${hex(curPc)}: ${hex(opcode)}");`);
          lines.push("};");
          this.translated++;
          return new Function(lines.join("\n"))();
      }

      curPc = seqPc;
    }

    lines.push("  return state.regs[15] >>> 0;");
    lines.push("};");
    this.translated++;
    return new Function(lines.join("\n"))();
  }

  step() {
    const pc = this.state.regs[15] >>> 0;
    let block = this.blockCache.get(pc);
    if (!block) {
      block = this.compileBlock(pc);
      this.blockCache.set(pc, block);
    }
    this.state.blocksExecuted++;
    return block(this.state, this.memory, {
      execDataProc,
      execSingleTransfer,
      execBlockTransfer,
      execMultiply,
      execBranch,
      execBx,
    });
  }

  run(maxBlocks) {
    for (let i = 0; i < maxBlocks; i++) {
      const result = this.memory.readResult();
      if (result.magic === DHRY_RESULT_MAGIC) {
        return result;
      }
      this.step();
    }

    throw new Error(`benchmark did not finish within ${maxBlocks} block executions`);
  }
}

function printResult(result, runner) {
  console.log(
    `magic=${hex(result.magic)} status=${result.status} iterations=${result.iterations} ` +
      `int_glob=${result.intGlob} bool_glob=${result.boolGlob} ` +
      `ch1=${String.fromCharCode(result.ch1)} ch2=${String.fromCharCode(result.ch2)} ` +
      `arr1[8]=${result.arr1_8} arr2[8][7]=${result.arr2_8_7} ` +
      `ptr=${result.ptrIntComp} next=${result.nextPtrIntComp} ` +
      `blocks=${runner.state.blocksExecuted} translated=${runner.translated} cache=${runner.blockCache.size}`
  );
}

function validateResult(result) {
  if (result.magic !== DHRY_RESULT_MAGIC) {
    throw new Error("missing result magic");
  }
  if (result.status !== DHRY_STATUS_PASS) {
    throw new Error(`benchmark status ${result.status} != ${DHRY_STATUS_PASS}`);
  }
}

function main() {
  const opts = parseArgs(process.argv);
  const runner = new JitRunner(opts.rom);
  const result = runner.run(opts.maxBlocks);
  validateResult(result);
  printResult(result, runner);
}

main();
