"use strict";

const REG_SP = 13;
const REG_LR = 14;
const REG_PC = 15;
const REG_CPSR = 16;
const CPU_MODE = 17;
const CPU_HALT_STATE = 18;
const REG_BUS_VALUE = 19;

const MODE_USER = 0x00;
const MODE_SYSTEM = 0x10;
const MODE_IRQ = 0x11;
const MODE_FIQ = 0x12;
const MODE_SUPERVISOR = 0x13;
const MODE_ABORT = 0x14;
const MODE_UNDEFINED = 0x15;
const MODE_INVALID = 0x16;

const CPU_ACTIVE = 0;
const CPU_HALT = 1;
const CPU_STOP = 2;
const CPU_DMA = 3;

const CPU_ALERT_NONE = 0;
const CPU_ALERT_HALT = 1 << 0;
const CPU_ALERT_SMC = 1 << 1;
const CPU_ALERT_IRQ = 1 << 2;

const THUMB_BIT = 0x20;

const CPSR_MASKS = [
  [0x00000000, 0x00000000],
  [0x00000020, 0x000000ef],
  [0xf0000000, 0xf0000000],
  [0xf0000020, 0xf00000ef],
];

const SPSR_MASKS = [0x00000000, 0x000000ef, 0xf0000000, 0xf00000ef];

function hex32(value) {
  return `0x${(value >>> 0).toString(16).padStart(8, "0")}`;
}

function ror32(value, shift) {
  const amount = shift & 31;
  if (amount === 0) {
    return value >>> 0;
  }
  return ((value >>> amount) | (value << (32 - amount))) >>> 0;
}

function asr32(value, shift) {
  return (value >> shift) >>> 0;
}

function signExtend(value, bits) {
  const shift = 32 - bits;
  return ((value << shift) >> shift) >>> 0;
}

function popcount16(value) {
  value &= 0xffff;
  let count = 0;
  while (value !== 0) {
    value &= value - 1;
    count++;
  }
  return count;
}

function mapCpuMode(modeNibble) {
  switch (modeNibble & 0xf) {
    case 0x0:
      return MODE_USER;
    case 0x1:
      return MODE_FIQ;
    case 0x2:
      return MODE_IRQ;
    case 0x3:
      return MODE_SUPERVISOR;
    case 0x7:
      return MODE_ABORT;
    case 0xb:
      return MODE_UNDEFINED;
    case 0xf:
      return MODE_SYSTEM;
    default:
      return MODE_INVALID;
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

function addWithCarry(a, b, carryIn) {
  const ua = a >>> 0;
  const ub = b >>> 0;
  const carry = carryIn >>> 0;
  const sum = ua + ub + carry;
  const result = sum >>> 0;
  const carryOut = sum > 0xffffffff ? 1 : 0;
  const overflow =
    (((ua ^ ub) & 0x80000000) === 0 && ((ua ^ result) & 0x80000000) !== 0)
      ? 1
      : 0;

  return { result, carryOut, overflow };
}

function setNZ(cpsr, value) {
  let next = cpsr & 0x0fffffff;
  if (value & 0x80000000) {
    next |= 0x80000000;
  }
  if ((value >>> 0) === 0) {
    next |= 0x40000000;
  }
  return next >>> 0;
}

function setNZC(cpsr, value, carry) {
  let next = setNZ(cpsr, value);
  next &= ~0x20000000;
  if (carry) {
    next |= 0x20000000;
  }
  return next >>> 0;
}

function setNZCV(cpsr, value, carry, overflow) {
  let next = setNZC(cpsr, value, carry);
  next &= ~0x10000000;
  if (overflow) {
    next |= 0x10000000;
  }
  return next >>> 0;
}

function shiftImm(value, type, amount, carryIn) {
  value >>>= 0;

  switch (type) {
    case 0:
      if (amount === 0) {
        return { value, carry: carryIn >>> 0 };
      }
      return {
        value: (value << amount) >>> 0,
        carry: (value >>> (32 - amount)) & 1,
      };
    case 1:
      if (amount === 0) {
        return { value: 0, carry: (value >>> 31) & 1 };
      }
      return {
        value: value >>> amount,
        carry: (value >>> (amount - 1)) & 1,
      };
    case 2:
      if (amount === 0) {
        const carry = (value >>> 31) & 1;
        return { value: carry ? 0xffffffff : 0, carry };
      }
      return {
        value: asr32(value | 0, amount),
        carry: (value >>> (amount - 1)) & 1,
      };
    case 3:
      if (amount === 0) {
        const result = ((carryIn << 31) | (value >>> 1)) >>> 0;
        return { value: result, carry: value & 1 };
      }
      return {
        value: ror32(value, amount),
        carry: (value >>> ((amount & 31) - 1)) & 1,
      };
    default:
      throw new Error(`invalid immediate shift type ${type}`);
  }
}

function shiftReg(value, type, amount, carryIn) {
  value >>>= 0;
  amount &= 0xff;

  if (amount === 0) {
    return { value, carry: carryIn >>> 0 };
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
      const rotate = amount & 31;
      if (rotate === 0) {
        return { value, carry: (value >>> 31) & 1 };
      }
      return {
        value: ror32(value, rotate),
        carry: (value >>> (rotate - 1)) & 1,
      };
    }
    default:
      throw new Error(`invalid register shift type ${type}`);
  }
}

module.exports = {
  REG_SP,
  REG_LR,
  REG_PC,
  REG_CPSR,
  CPU_MODE,
  CPU_HALT_STATE,
  REG_BUS_VALUE,
  MODE_USER,
  MODE_SYSTEM,
  MODE_IRQ,
  MODE_FIQ,
  MODE_SUPERVISOR,
  MODE_ABORT,
  MODE_UNDEFINED,
  MODE_INVALID,
  CPU_ACTIVE,
  CPU_HALT,
  CPU_STOP,
  CPU_DMA,
  CPU_ALERT_NONE,
  CPU_ALERT_HALT,
  CPU_ALERT_SMC,
  CPU_ALERT_IRQ,
  THUMB_BIT,
  CPSR_MASKS,
  SPSR_MASKS,
  hex32,
  ror32,
  asr32,
  signExtend,
  popcount16,
  mapCpuMode,
  condPassed,
  addWithCarry,
  setNZ,
  setNZC,
  setNZCV,
  shiftImm,
  shiftReg,
};
