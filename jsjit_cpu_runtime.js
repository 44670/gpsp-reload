"use strict";

const { createGpspJsJitBridge } = require("./jsjit_bridge_runtime.js");
const { buildArmBlockExecutor, createArmBlockFactory } = require("./jsjit_arm_codegen.js");
const { buildThumbBlockExecutor, createThumbBlockFactory } = require("./jsjit_thumb_codegen.js");
const {
  REG_SP,
  REG_LR,
  REG_PC,
  REG_CPSR,
  CPU_MODE,
  CPU_HALT_STATE,
  REG_BUS_VALUE,
  MODE_USER,
  MODE_FIQ,
  MODE_IRQ,
  MODE_SYSTEM,
  MODE_SUPERVISOR,
  MODE_ABORT,
  MODE_UNDEFINED,
  CPU_ACTIVE,
  CPU_ALERT_HALT,
  CPU_ALERT_SMC,
  CPU_ALERT_IRQ,
  THUMB_BIT,
  CPSR_MASKS,
  SPSR_MASKS,
  ror32,
  mapCpuMode,
  signExtend,
  condPassed,
  addWithCarry,
  setNZ,
  setNZC,
  setNZCV,
  shiftImm,
  shiftReg,
  popcount16,
} = require("./jsjit_common.js");

const MAX_ARM_BLOCK_INSTRUCTIONS = 48;
const MAX_THUMB_BLOCK_INSTRUCTIONS = 64;

function createGpspJsJitRuntime(Module) {
  const bridge = createGpspJsJitBridge(Module);

  const runtime = {
    Module,
    bridge,
    regs: bridge.regs,
    spsr: bridge.spsr,
    regMode: bridge.regMode,
    armBlocks: new Map(),
    thumbBlocks: new Map(),
    armBlockFactory: null,
    thumbBlockFactory: null,
    cyclesRemaining: 0,
    pendingAlert: 0,
    maxArmBlockInstructions: MAX_ARM_BLOCK_INSTRUCTIONS,
    maxThumbBlockInstructions: MAX_THUMB_BLOCK_INSTRUCTIONS,
    stats: {
      executions: 0,
      fallbackExecutions: 0,
      fallbackCycles: 0,
    },

    refresh() {
      bridge.refreshViews();
      this.regs = bridge.regs;
      this.spsr = bridge.spsr;
      this.regMode = bridge.regMode;
      return this;
    },

    reset() {
      this.refresh();
      this.armBlocks.clear();
      this.thumbBlocks.clear();
      this.cyclesRemaining = 0;
      this.pendingAlert = 0;
      bridge.takePendingAlert();
      this.stats.executions = 0;
      this.stats.fallbackExecutions = 0;
      this.stats.fallbackCycles = 0;
      return this;
    },

    clearCaches() {
      this.armBlocks.clear();
      this.thumbBlocks.clear();
      return this;
    },

    ensureBlockFactories() {
      if (!this.armBlockFactory) {
        this.armBlockFactory = createArmBlockFactory({
          runtime: this,
          read8: bridge.readMemory8,
          read16: bridge.readMemory16,
          read32: bridge.readMemory32,
          write8: bridge.writeMemory8,
          write16: bridge.writeMemory16,
          write32: bridge.writeMemory32,
          signExtend,
          condPassed,
          addWithCarry,
          setNZ,
          setNZC,
          setNZCV,
          shiftImm,
          shiftReg,
          ror32,
        });
      }
      if (!this.thumbBlockFactory) {
        this.thumbBlockFactory = createThumbBlockFactory({
          runtime: this,
          read8: bridge.readMemory8,
          read16: bridge.readMemory16,
          read32: bridge.readMemory32,
          write8: bridge.writeMemory8,
          write16: bridge.writeMemory16,
          write32: bridge.writeMemory32,
          signExtend,
          condPassed,
          addWithCarry,
          setNZ,
          setNZC,
          setNZCV,
          shiftImm,
          shiftReg,
          applyCpsr: this.applyCpsr.bind(this),
        });
      }
      return this;
    },

    regModeIndex(mode, slot) {
      return (((mode & 0x0f) * 7) + slot) >>> 0;
    },

    setCpuMode(newMode) {
      const oldMode = this.regs[CPU_MODE] >>> 0;
      const nextMode = newMode >>> 0;
      if (oldMode === nextMode) {
        return;
      }

      if (nextMode === MODE_FIQ) {
        for (let reg = 8; reg < 15; reg++) {
          this.regMode[this.regModeIndex(oldMode, reg - 8)] = this.regs[reg] >>> 0;
        }
      } else {
        this.regMode[this.regModeIndex(oldMode, 5)] = this.regs[REG_SP] >>> 0;
        this.regMode[this.regModeIndex(oldMode, 6)] = this.regs[REG_LR] >>> 0;
      }

      if (oldMode === MODE_FIQ) {
        for (let reg = 8; reg < 15; reg++) {
          this.regs[reg] = this.regMode[this.regModeIndex(nextMode, reg - 8)] >>> 0;
        }
      } else {
        this.regs[REG_SP] = this.regMode[this.regModeIndex(nextMode, 5)] >>> 0;
        this.regs[REG_LR] = this.regMode[this.regModeIndex(nextMode, 6)] >>> 0;
      }

      this.regs[CPU_MODE] = nextMode >>> 0;
    },

    ensureViews() {
      const heaps = bridge.getHeapViews();
      if (bridge.heapU8 !== heaps.HEAPU8 || bridge.heapU8.buffer !== heaps.HEAPU8.buffer) {
        this.refresh();
        this.clearCaches();
      }
      return this;
    },

    isGamepakAddress(address) {
      const region = (address >>> 24) & 0xff;
      return region >= 0x08 && region <= 0x0d;
    },

    isMutableCodeRegion(address) {
      return ((address >>> 24) & 0xff) < 0x08;
    },

    readHeap16(ptr) {
      const heap = bridge.heapU8;
      return (heap[ptr] | (heap[ptr + 1] << 8)) >>> 0;
    },

    readHeap32(ptr) {
      const heap = bridge.heapU8;
      return (
        heap[ptr] |
        (heap[ptr + 1] << 8) |
        (heap[ptr + 2] << 16) |
        (heap[ptr + 3] << 24)
      ) >>> 0;
    },

    ensureReadPage(address) {
      const pageIndex = address >>> bridge.pageShift;
      let pageBase = bridge.memoryMapRead[pageIndex] >>> 0;
      if (pageBase === 0 && this.isGamepakAddress(address)) {
        bridge.loadGamepakPage(pageIndex & 0x3ff);
        pageBase = bridge.memoryMapRead[pageIndex] >>> 0;
      }
      return pageBase >>> 0;
    },

    fetchArmOpcode(address) {
      const aligned = address & ~3;
      const pageBase = this.ensureReadPage(aligned);
      if (pageBase === 0) {
        return bridge.readMemory32(aligned) >>> 0;
      }
      return this.readHeap32((pageBase + (aligned & (bridge.pageSize - 1))) >>> 0);
    },

    fetchThumbOpcode(address) {
      const aligned = address & ~1;
      const pageBase = this.ensureReadPage(aligned);
      if (pageBase === 0) {
        return bridge.readMemory16(aligned) & 0xffff;
      }
      return this.readHeap16((pageBase + (aligned & (bridge.pageSize - 1))) >>> 0) & 0xffff;
    },

    isTranslationGateTarget(address) {
      const count = bridge.translationGateTargets[0] >>> 0;
      for (let i = 0; i < count; i++) {
        if ((bridge.translationGateTargetPc[i] >>> 0) === (address >>> 0)) {
          return true;
        }
      }
      return false;
    },

    validateBlock(block) {
      if (!block || !block.mutable) {
        return true;
      }

      for (let i = 0; i < block.steps.length; i++) {
        const step = block.steps[i];
        const current = block.thumb
          ? this.fetchThumbOpcode(step.pc)
          : this.fetchArmOpcode(step.pc);
        if ((current >>> 0) !== (step.opcode >>> 0)) {
          return false;
        }
      }

      return true;
    },

    wsIndex(wordSized) {
      return wordSized ? 1 : 0;
    },

    addCyclesFromTable(table, address, wordSized) {
      const aligned = address >>> 0;
      if (aligned < 0x10000000) {
        const index = ((aligned >>> 24) << 1) | this.wsIndex(wordSized);
        this.cyclesRemaining -= table[index] >>> 0;
      }
    },

    addNseqCycles(address, wordSized) {
      this.addCyclesFromTable(bridge.wsCycNseq, address, wordSized);
    },

    addSeqCycles(address, wordSized) {
      this.addCyclesFromTable(bridge.wsCycSeq, address, wordSized);
    },

    readSigned8(address) {
      return signExtend(bridge.readMemory8(address >>> 0) & 0xff, 8);
    },

    readSigned16(address) {
      const aligned = address >>> 0;
      if (aligned & 1) {
        return signExtend(bridge.readMemory8(aligned) & 0xff, 8);
      }
      return signExtend(bridge.readMemory16(aligned) & 0xffff, 16);
    },

    writeMemory8(address, value) {
      const alert = bridge.writeMemory8(address >>> 0, value >>> 0) >>> 0;
      this.pendingAlert |= alert;
      return alert;
    },

    writeMemory16(address, value) {
      const alert = bridge.writeMemory16(address >>> 0, value >>> 0) >>> 0;
      this.pendingAlert |= alert;
      return alert;
    },

    writeMemory32(address, value) {
      const alert = bridge.writeMemory32(address >>> 0, value >>> 0) >>> 0;
      this.pendingAlert |= alert;
      return alert;
    },

    processPendingAlert() {
      const alert = (this.pendingAlert | bridge.takePendingAlert()) >>> 0;
      if (alert === 0) {
        return;
      }

      this.pendingAlert = 0;
      if (alert & CPU_ALERT_SMC) {
        this.clearCaches();
      }
    },

    applyCpsrNoIrq(value) {
      const next = value >>> 0;
      this.regs[REG_CPSR] = next;

      const newMode = mapCpuMode(next & 0x0f);
      if (newMode !== this.regs[CPU_MODE] && newMode !== 0x16) {
        this.setCpuMode(newMode);
      }
    },

    applyCpsr(value, checkInterrupts) {
      this.applyCpsrNoIrq(value);

      if (checkInterrupts) {
        this.pendingAlert |= CPU_ALERT_IRQ;
      }
    },

    restoreSpsrToCpsrNoIrq() {
      const mode = this.regs[CPU_MODE] >>> 0;
      if (mode === MODE_USER || mode === MODE_SYSTEM) {
        return;
      }
      this.applyCpsrNoIrq(this.spsr[mode & 0x0f] >>> 0);
    },

    restoreSpsrToCpsr() {
      this.restoreSpsrToCpsrNoIrq();
      this.pendingAlert |= CPU_ALERT_IRQ;
    },

    readArmReg(regIndex, pc, kind) {
      if (regIndex === REG_PC) {
        if (kind === "shiftreg") {
          return (pc + 12) >>> 0;
        }
        return (pc + 8) >>> 0;
      }
      return this.regs[regIndex] >>> 0;
    },

    readThumbHiReg(regIndex, pc) {
      if (regIndex === REG_PC) {
        return (pc + 4) >>> 0;
      }
      return this.regs[regIndex] >>> 0;
    },

    decodeArmOperand2(pc, opcode) {
      const carryIn = (this.regs[REG_CPSR] >>> 29) & 1;

      if ((opcode >>> 25) & 1) {
        const imm8 = opcode & 0xff;
        const rotate = ((opcode >>> 8) & 0xf) * 2;
        if (rotate === 0) {
          return { value: imm8 >>> 0, carry: carryIn };
        }
        return shiftImm(imm8 >>> 0, 3, rotate, carryIn);
      }

      const rm = opcode & 0xf;
      const value = this.readArmReg(rm, pc, (opcode & 0x10) ? "shiftreg" : "shift");
      const shiftType = (opcode >>> 5) & 0x3;

      if (opcode & 0x10) {
        const rs = (opcode >>> 8) & 0xf;
        const amount = this.readArmReg(rs, pc, "shiftreg");
        return shiftReg(value, shiftType, amount, carryIn);
      }

      return shiftImm(value, shiftType, (opcode >>> 7) & 0x1f, carryIn);
    },

    decodeArmTransferOffset(pc, opcode) {
      if (((opcode >>> 25) & 1) === 0) {
        return opcode & 0x0fff;
      }

      const rm = opcode & 0xf;
      const value = this.readArmReg(rm, pc, "addr");
      const shiftType = (opcode >>> 5) & 0x3;
      return shiftImm(value, shiftType, (opcode >>> 7) & 0x1f, (this.regs[REG_CPSR] >>> 29) & 1).value >>> 0;
    },

    execArmDataProc(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const op = (opcode >>> 21) & 0xf;
      const setFlags = (opcode >>> 20) & 1;
      const rn = (opcode >>> 16) & 0xf;
      const rd = (opcode >>> 12) & 0xf;
      const lhs = this.readArmReg(rn, pc, "alu");
      const sh = this.decodeArmOperand2(pc, opcode);
      const rhs = sh.value >>> 0;
      let result = 0;

      switch (op) {
        case 0x0:
          result = lhs & rhs;
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        case 0x1:
          result = lhs ^ rhs;
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        case 0x2: {
          const sub = addWithCarry(lhs, (~rhs) >>> 0, 1);
          result = sub.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, sub.carryOut, sub.overflow);
          break;
        }
        case 0x3: {
          const sub = addWithCarry(rhs, (~lhs) >>> 0, 1);
          result = sub.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, sub.carryOut, sub.overflow);
          break;
        }
        case 0x4: {
          const add = addWithCarry(lhs, rhs, 0);
          result = add.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, add.carryOut, add.overflow);
          break;
        }
        case 0x5: {
          const add = addWithCarry(lhs, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          result = add.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, add.carryOut, add.overflow);
          break;
        }
        case 0x6: {
          const sub = addWithCarry(lhs, (~rhs) >>> 0, (this.regs[REG_CPSR] >>> 29) & 1);
          result = sub.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, sub.carryOut, sub.overflow);
          break;
        }
        case 0x7: {
          const sub = addWithCarry(rhs, (~lhs) >>> 0, (this.regs[REG_CPSR] >>> 29) & 1);
          result = sub.result;
          if (setFlags) this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], result, sub.carryOut, sub.overflow);
          break;
        }
        case 0x8:
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], lhs & rhs, sh.carry);
          this.regs[REG_PC] = nextPc;
          return this.completeArmInstruction(false, false);
        case 0x9:
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], lhs ^ rhs, sh.carry);
          this.regs[REG_PC] = nextPc;
          return this.completeArmInstruction(false, false);
        case 0xa: {
          const sub = addWithCarry(lhs, (~rhs) >>> 0, 1);
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], sub.result, sub.carryOut, sub.overflow);
          this.regs[REG_PC] = nextPc;
          return this.completeArmInstruction(false, false);
        }
        case 0xb: {
          const add = addWithCarry(lhs, rhs, 0);
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], add.result, add.carryOut, add.overflow);
          this.regs[REG_PC] = nextPc;
          return this.completeArmInstruction(false, false);
        }
        case 0xc:
          result = lhs | rhs;
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        case 0xd:
          result = rhs;
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        case 0xe:
          result = lhs & (~rhs);
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        case 0xf:
          result = (~rhs) >>> 0;
          if (setFlags) this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], result, sh.carry);
          break;
        default:
          throw new Error(`unsupported ARM data-proc op ${op.toString(16)}`);
      }

      if (rd === REG_PC) {
        this.regs[REG_PC] = result >>> 0;
        if (setFlags) {
          this.restoreSpsrToCpsr();
          return this.completeArmInstruction(true, (this.regs[REG_CPSR] & THUMB_BIT) !== 0);
        }
        return this.completeArmInstruction(true, false);
      }

      this.regs[rd] = result >>> 0;
      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(false, false);
    },

    execArmBranch(pc, opcode) {
      let offset = opcode & 0x00ffffff;
      if (offset & 0x00800000) {
        offset |= 0xff000000;
      }
      const target = (pc + 8 + ((offset << 2) >> 0)) >>> 0;
      if ((opcode >>> 24) & 1) {
        this.regs[REG_LR] = (pc + 4) >>> 0;
      }
      this.regs[REG_PC] = target;
      this.addNseqCycles(target, true);
      return this.completeArmInstruction(true, false);
    },

    execArmBx(pc, opcode) {
      const target = this.readArmReg(opcode & 0xf, pc, "bx");
      if (target & 1) {
        this.regs[REG_PC] = (target - 1) >>> 0;
        this.regs[REG_CPSR] |= THUMB_BIT;
        return this.completeArmInstruction(true, true);
      }

      this.regs[REG_PC] = target >>> 0;
      this.addNseqCycles(target, true);
      return this.completeArmInstruction(true, false);
    },

    execArmSwi(pc) {
      this.regs[REG_BUS_VALUE] = 0xe3a02004;
      this.regMode[(MODE_SUPERVISOR & 0x0f) * 7 + 6] = (pc + 4) >>> 0;
      this.spsr[MODE_SUPERVISOR & 0x0f] = this.regs[REG_CPSR] >>> 0;
      this.regs[REG_PC] = 0x00000008;
      this.applyCpsr(((this.regs[REG_CPSR] & ~0x3f) | 0x13 | 0x80) >>> 0, false);
      return this.completeArmInstruction(true, false);
    },

    execArmPsr(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const top = (opcode >>> 20) & 0xff;
      const pfield = ((opcode >>> 16) & 1) | ((opcode >>> 18) & 2);
      const mode = this.regs[CPU_MODE] >>> 0;

      if (top === 0x10 || top === 0x14) {
        const rd = (opcode >>> 12) & 0xf;
        const value =
          top === 0x14
            ? (this.spsr[mode & 0x0f] >>> 0)
            : (this.regs[REG_CPSR] >>> 0);
        this.regs[rd] = value;
        this.regs[REG_PC] = nextPc;
        return this.completeArmInstruction(true, false);
      }

      const source =
        top === 0x32 || top === 0x36
          ? (((opcode >>> 8) & 0x0f) === 0
              ? (opcode & 0xff) >>> 0
              : ror32(opcode & 0xff, ((opcode >>> 8) & 0x0f) * 2))
          : (this.regs[opcode & 0x0f] >>> 0);

      if (top === 0x12 || top === 0x32) {
        const mask = CPSR_MASKS[pfield][(mode >>> 4) & 1] >>> 0;
        const next = ((source & mask) | (this.regs[REG_CPSR] & (~mask >>> 0))) >>> 0;
        this.applyCpsr(next, (mask & 0xff) !== 0);
      } else {
        if (mode !== MODE_USER && mode !== MODE_SYSTEM) {
          const mask = SPSR_MASKS[pfield] >>> 0;
          const slot = mode & 0x0f;
          this.spsr[slot] = ((source & mask) | (this.spsr[slot] & (~mask >>> 0))) >>> 0;
        }
      }

      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(true, false);
    },

    execArmMultiply(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const accumulate = ((opcode >>> 21) & 1) !== 0;
      const setFlags = ((opcode >>> 20) & 1) !== 0;

      if ((opcode & 0x00800000) === 0) {
        const rd = (opcode >>> 16) & 0xf;
        const rn = (opcode >>> 12) & 0xf;
        const rs = (opcode >>> 8) & 0xf;
        const rm = opcode & 0xf;
        let result = Math.imul(this.regs[rm] | 0, this.regs[rs] | 0) >>> 0;
        if (accumulate) {
          result = (result + (this.regs[rn] >>> 0)) >>> 0;
        }
        this.regs[rd] = result;
        if (setFlags) {
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], result);
        }
        this.regs[REG_PC] = nextPc;
        return this.completeArmInstruction(true, false);
      }

      const signed = ((opcode >>> 22) & 1) !== 0;
      const rdHi = (opcode >>> 16) & 0xf;
      const rdLo = (opcode >>> 12) & 0xf;
      const rs = (opcode >>> 8) & 0xf;
      const rm = opcode & 0xf;
      const a = signed ? BigInt.asIntN(32, BigInt(this.regs[rm] >>> 0)) : BigInt(this.regs[rm] >>> 0);
      const b = signed ? BigInt.asIntN(32, BigInt(this.regs[rs] >>> 0)) : BigInt(this.regs[rs] >>> 0);
      let value = a * b;

      if (accumulate) {
        const current =
          (BigInt(this.regs[rdHi] >>> 0) << 32n) | BigInt(this.regs[rdLo] >>> 0);
        value += signed ? BigInt.asIntN(64, current) : current;
      }

      const unsigned64 = BigInt.asUintN(64, value);
      const lo = Number(unsigned64 & 0xffffffffn) >>> 0;
      const hi = Number((unsigned64 >> 32n) & 0xffffffffn) >>> 0;
      this.regs[rdLo] = lo;
      this.regs[rdHi] = hi;

      if (setFlags) {
        let cpsr = this.regs[REG_CPSR] & 0x3fffffff;
        if (hi & 0x80000000) {
          cpsr |= 0x80000000;
        }
        if (((lo | hi) >>> 0) === 0) {
          cpsr |= 0x40000000;
        }
        this.regs[REG_CPSR] = cpsr >>> 0;
      }

      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(true, false);
    },

    execArmSwap(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const byte = (opcode & 0x00400000) !== 0;
      const rn = (opcode >>> 16) & 0xf;
      const rd = (opcode >>> 12) & 0xf;
      const rm = opcode & 0xf;
      const address = this.regs[rn] >>> 0;
      const source = this.regs[rm] >>> 0;
      const value = byte ? (bridge.readMemory8(address) & 0xff) : (bridge.readMemory32(address) >>> 0);

      this.addNseqCycles(address, !byte);
      if (byte) {
        this.writeMemory8(address, source);
      } else {
        this.writeMemory32(address, source);
      }
      this.addNseqCycles(address, !byte);

      this.regs[rd] = value >>> 0;
      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(true, false);
    },

    execArmHalfTransfer(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const pre = ((opcode >>> 24) & 1) !== 0;
      const up = ((opcode >>> 23) & 1) !== 0;
      const immediate = ((opcode >>> 22) & 1) !== 0;
      const writeBack = ((opcode >>> 21) & 1) !== 0;
      const load = ((opcode >>> 20) & 1) !== 0;
      const rn = (opcode >>> 16) & 0xf;
      const rd = (opcode >>> 12) & 0xf;
      const sh = (opcode >>> 5) & 0x3;
      const rawOffset = immediate
        ? (((opcode >>> 4) & 0xf0) | (opcode & 0x0f)) >>> 0
        : (this.readArmReg(opcode & 0x0f, pc, "addr") >>> 0);
      const base = this.readArmReg(rn, pc, "addr");
      const offset = up ? rawOffset : ((-rawOffset) >>> 0);
      const address = pre ? ((base + offset) >>> 0) : base;
      const writeBackValue = pre ? address : ((base + offset) >>> 0);

      if (load) {
        let value;
        if (sh === 1) {
          value = bridge.readMemory16(address) & 0xffff;
        } else if (sh === 2) {
          value = this.readSigned8(address);
        } else if (sh === 3) {
          value = this.readSigned16(address);
        } else {
          throw new Error(`unsupported ARM halfword load variant 0x${opcode.toString(16)}`);
        }

        this.addNseqCycles(address, false);
        if (rd === REG_PC) {
          this.regs[REG_PC] = value >>> 0;
        } else {
          this.regs[rd] = value >>> 0;
        }
      } else {
        const value = rd === REG_PC ? ((pc + 12) >>> 0) : (this.regs[rd] >>> 0);
        if (sh !== 1) {
          throw new Error(`unsupported ARM halfword store variant 0x${opcode.toString(16)}`);
        }
        this.writeMemory16(address, value);
        this.addNseqCycles(address, false);
      }

      if (writeBack || !pre) {
        if (rn !== REG_PC) {
          this.regs[rn] = writeBackValue >>> 0;
        }
      }

      if (load && rd === REG_PC) {
        return this.completeArmInstruction(true, false);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(true, false);
    },

    execArmSingleTransfer(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const pre = ((opcode >>> 24) & 1) !== 0;
      const up = ((opcode >>> 23) & 1) !== 0;
      const byte = ((opcode >>> 22) & 1) !== 0;
      const writeBack = ((opcode >>> 21) & 1) !== 0;
      const load = ((opcode >>> 20) & 1) !== 0;
      const rn = (opcode >>> 16) & 0xf;
      const rd = (opcode >>> 12) & 0xf;
      const base = this.readArmReg(rn, pc, "addr");
      const rawOffset = this.decodeArmTransferOffset(pc, opcode);
      const offset = up ? rawOffset : ((-rawOffset) >>> 0);
      const address = pre ? ((base + offset) >>> 0) : base;
      const writeBackValue = pre ? address : ((base + offset) >>> 0);

      if (load) {
        const value = byte
          ? (bridge.readMemory8(address) & 0xff)
          : (bridge.readMemory32(address) >>> 0);
        this.addNseqCycles(address, !byte);
        if (rd === REG_PC) {
          this.regs[REG_PC] = value >>> 0;
        } else {
          this.regs[rd] = value >>> 0;
        }
      } else {
        const value = rd === REG_PC ? ((pc + 12) >>> 0) : (this.regs[rd] >>> 0);
        if (byte) {
          this.writeMemory8(address, value);
        } else {
          this.writeMemory32(address, value);
        }
        this.addNseqCycles(address, !byte);
      }

      if (writeBack || !pre) {
        if (rn !== REG_PC) {
          this.regs[rn] = writeBackValue >>> 0;
        }
      }

      if (load && rd === REG_PC) {
        return this.completeArmInstruction(true, false);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(false, false);
    },

    execArmBlockTransfer(pc, opcode) {
      const nextPc = (pc + 4) >>> 0;
      const pre = ((opcode >>> 24) & 1) !== 0;
      const up = ((opcode >>> 23) & 1) !== 0;
      const sbit = ((opcode >>> 22) & 1) !== 0;
      const writeBack = ((opcode >>> 21) & 1) !== 0;
      const load = ((opcode >>> 20) & 1) !== 0;
      const rn = (opcode >>> 16) & 0xf;
      const regList = opcode & 0xffff;
      const count = popcount16(regList);
      const base = this.regs[rn] >>> 0;
      const addrDelta = up ? 4 : -4;
      const endAddr = (base + (addrDelta * count)) >>> 0;
      let address;
      const oldCpsr = this.regs[REG_CPSR] >>> 0;
      const oldMode = this.regs[CPU_MODE] >>> 0;

      if (pre) {
        address = up ? ((base + 4) >>> 0) : endAddr;
      } else {
        address = up ? base : ((endAddr + 4) >>> 0);
      }
      address &= ~3;

      if (sbit && (!load || rn !== REG_PC)) {
        this.setCpuMode(MODE_USER);
      }

      const baseMask = (1 << rn) >>> 0;
      const baseFirst = ((((1 << rn) >>> 0) - 1) & regList) === 0;
      const writeBackFirst = load || !((regList & baseMask) !== 0 && baseFirst);

      if (writeBack && writeBackFirst) {
        this.regs[rn] = endAddr >>> 0;
      }

      this.regs[REG_PC] = nextPc;

      for (let reg = 0; reg < 16; reg++) {
        if (((regList >>> reg) & 1) === 0) {
          continue;
        }

        if (load) {
          this.regs[reg] = bridge.readMemory32(address) >>> 0;
          this.addSeqCycles(address, true);
        } else {
          const value = reg === REG_PC ? ((this.regs[REG_PC] + 4) >>> 0) : (this.regs[reg] >>> 0);
          this.writeMemory32(address, value);
          this.addSeqCycles(address, true);
        }

        address = (address + 4) >>> 0;
      }

      if (writeBack && !writeBackFirst) {
        this.regs[rn] = endAddr >>> 0;
      }

      if (sbit && (!load || rn !== REG_PC)) {
        if (oldMode !== this.regs[CPU_MODE]) {
          this.setCpuMode(oldMode);
        }
        this.regs[REG_CPSR] = oldCpsr >>> 0;
      }

      if (load && (regList & (1 << REG_PC)) !== 0) {
        if (sbit) {
          this.restoreSpsrToCpsr();
          return this.completeArmInstruction(true, (this.regs[REG_CPSR] & THUMB_BIT) !== 0);
        }
        return this.completeArmInstruction(true, false);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeArmInstruction(true, false);
    },

    execArmOpcode(pc, opcode) {
      if (!condPassed(this.regs[REG_CPSR] >>> 0, opcode >>> 28)) {
        this.regs[REG_PC] = (pc + 4) >>> 0;
        return this.completeArmInstruction(false, false);
      }

      if ((opcode & 0x0ffffff0) === 0x012fff10) {
        return this.execArmBx(pc, opcode);
      }
      if ((opcode & 0x0f000000) === 0x0f000000) {
        return this.execArmSwi(pc);
      }
      if ((opcode & 0x0e000000) === 0x0a000000) {
        return this.execArmBranch(pc, opcode);
      }
      if ((opcode & 0x0e000000) === 0x08000000) {
        return this.execArmBlockTransfer(pc, opcode);
      }
      if ((opcode & 0x0c000000) === 0x04000000) {
        return this.execArmSingleTransfer(pc, opcode);
      }
      if ((opcode & 0x0c000000) === 0x00000000) {
        if ((opcode & 0x02000090) === 0x00000090) {
          if ((opcode & 0x0fc000f0) === 0x00000090 || (opcode & 0x0f8000f0) === 0x00800090) {
            return this.execArmMultiply(pc, opcode);
          }
          if ((opcode & 0x0fb00ff0) === 0x01000090 || (opcode & 0x0fb00ff0) === 0x01400090) {
            return this.execArmSwap(pc, opcode);
          }
          return this.execArmHalfTransfer(pc, opcode);
        }

        if (this.isArmPsrOpcode(opcode)) {
          return this.execArmPsr(pc, opcode);
        }

        return this.execArmDataProc(pc, opcode);
      }

      throw new Error(`unsupported ARM opcode 0x${(opcode >>> 0).toString(16)}`);
    },

    execThumbShift(pc, opcode, shiftType) {
      const nextPc = (pc + 2) >>> 0;
      const rd = opcode & 0x07;
      const rs = (opcode >>> 3) & 0x07;
      const imm = (opcode >>> 6) & 0x1f;
      const sh = shiftImm(this.regs[rs] >>> 0, shiftType, imm, (this.regs[REG_CPSR] >>> 29) & 1);
      this.regs[rd] = sh.value >>> 0;
      this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], sh.value >>> 0, sh.carry);
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbAddSub(pc, opcode, subtract, immediate) {
      const nextPc = (pc + 2) >>> 0;
      const rd = opcode & 0x07;
      const rs = (opcode >>> 3) & 0x07;
      const rhs = immediate ? ((opcode >>> 6) & 0x07) : (this.regs[(opcode >>> 6) & 0x07] >>> 0);
      const lhs = this.regs[rs] >>> 0;
      const res = subtract
        ? addWithCarry(lhs, (~rhs) >>> 0, 1)
        : addWithCarry(lhs, rhs >>> 0, 0);

      this.regs[rd] = res.result >>> 0;
      this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result >>> 0, res.carryOut, res.overflow);
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbImmOp(pc, opcode, kind) {
      const nextPc = (pc + 2) >>> 0;
      const rd = (opcode >>> 8) & 0x07;
      const imm = opcode & 0xff;
      let res;

      switch (kind) {
        case "mov":
          this.regs[rd] = imm >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], imm >>> 0);
          break;
        case "cmp":
          res = addWithCarry(this.regs[rd] >>> 0, (~imm) >>> 0, 1);
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case "add":
          res = addWithCarry(this.regs[rd] >>> 0, imm >>> 0, 0);
          this.regs[rd] = res.result >>> 0;
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case "sub":
          res = addWithCarry(this.regs[rd] >>> 0, (~imm) >>> 0, 1);
          this.regs[rd] = res.result >>> 0;
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        default:
          throw new Error(`unsupported thumb imm op ${kind}`);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbAlu(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const rd = opcode & 0x07;
      const rs = (opcode >>> 3) & 0x07;
      const op = (opcode >>> 6) & 0x0f;
      const lhs = this.regs[rd] >>> 0;
      const rhs = this.regs[rs] >>> 0;
      let res;
      let shifted;

      switch (op) {
        case 0x0:
          this.regs[rd] = (lhs & rhs) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        case 0x1:
          this.regs[rd] = (lhs ^ rhs) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        case 0x2:
          shifted = shiftReg(lhs, 0, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = shifted.value >>> 0;
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], shifted.value >>> 0, shifted.carry);
          break;
        case 0x3:
          shifted = shiftReg(lhs, 1, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = shifted.value >>> 0;
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], shifted.value >>> 0, shifted.carry);
          break;
        case 0x4:
          shifted = shiftReg(lhs, 2, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = shifted.value >>> 0;
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], shifted.value >>> 0, shifted.carry);
          break;
        case 0x5:
          res = addWithCarry(lhs, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = res.result >>> 0;
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case 0x6:
          res = addWithCarry(lhs, (~rhs) >>> 0, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = res.result >>> 0;
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case 0x7:
          shifted = shiftReg(lhs, 3, rhs, (this.regs[REG_CPSR] >>> 29) & 1);
          this.regs[rd] = shifted.value >>> 0;
          this.regs[REG_CPSR] = setNZC(this.regs[REG_CPSR], shifted.value >>> 0, shifted.carry);
          break;
        case 0x8:
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], (lhs & rhs) >>> 0);
          break;
        case 0x9:
          res = addWithCarry(0, (~rhs) >>> 0, 1);
          this.regs[rd] = res.result >>> 0;
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case 0xa:
          res = addWithCarry(lhs, (~rhs) >>> 0, 1);
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case 0xb:
          res = addWithCarry(lhs, rhs, 0);
          this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
          break;
        case 0xc:
          this.regs[rd] = (lhs | rhs) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        case 0xd:
          this.regs[rd] = Math.imul(lhs | 0, rhs | 0) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        case 0xe:
          this.regs[rd] = (lhs & (~rhs >>> 0)) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        case 0xf:
          this.regs[rd] = (~rhs) >>> 0;
          this.regs[REG_CPSR] = setNZ(this.regs[REG_CPSR], this.regs[rd]);
          break;
        default:
          throw new Error(`unsupported thumb alu op ${op}`);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbHiReg(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const top = (opcode >>> 8) & 0xff;
      const rs = (opcode >>> 3) & 0x0f;
      const rd = (((opcode >>> 4) & 0x08) | (opcode & 0x07)) >>> 0;
      const lhs = this.readThumbHiReg(rd, pc);
      const rhs = this.readThumbHiReg(rs, pc);

      if (top === 0x44) {
        const result = (lhs + rhs) >>> 0;
        if (rd === REG_PC) {
          this.regs[REG_PC] = result & ~1;
          return this.completeThumbInstruction(true, false);
        }
        this.regs[rd] = result;
        this.regs[REG_PC] = nextPc;
        return this.completeThumbInstruction(false, false);
      }

      if (top === 0x45) {
        const res = addWithCarry(lhs, (~rhs) >>> 0, 1);
        this.regs[REG_CPSR] = setNZCV(this.regs[REG_CPSR], res.result, res.carryOut, res.overflow);
        this.regs[REG_PC] = nextPc;
        return this.completeThumbInstruction(false, false);
      }

      if (top === 0x46) {
        if (rd === REG_PC) {
          this.regs[REG_PC] = rhs & ~1;
          return this.completeThumbInstruction(true, false);
        }
        this.regs[rd] = rhs >>> 0;
        this.regs[REG_PC] = nextPc;
        return this.completeThumbInstruction(false, false);
      }

      if (rhs & 1) {
        this.regs[REG_PC] = (rhs - 1) >>> 0;
        return this.completeThumbInstruction(true, false);
      }

      this.regs[REG_PC] = rhs >>> 0;
      this.regs[REG_CPSR] &= ~THUMB_BIT;
      return this.completeThumbInstruction(true, true);
    },

    execThumbCondBranch(pc, opcode) {
      const cond = (opcode >>> 8) & 0x0f;
      const offset = signExtend(opcode & 0xff, 8);
      const taken = condPassed(this.regs[REG_CPSR] >>> 0, cond);
      this.regs[REG_PC] = taken ? ((pc + 4 + ((offset << 1) >> 0)) >>> 0) : ((pc + 2) >>> 0);
      this.addNseqCycles(this.regs[REG_PC] >>> 0, false);
      return this.completeThumbInstruction(true, false);
    },

    execThumbBranch(pc, opcode) {
      let offset = opcode & 0x07ff;
      if (offset & 0x0400) {
        offset |= ~0x07ff;
      }
      this.regs[REG_PC] = (pc + 4 + ((offset << 1) >> 0)) >>> 0;
      this.addNseqCycles(this.regs[REG_PC] >>> 0, false);
      return this.completeThumbInstruction(true, false);
    },

    execThumbBlPrefix(pc, opcode) {
      const offset = opcode & 0x07ff;
      this.regs[REG_LR] = (pc + 4 + ((offset << 21) >> 9)) >>> 0;
      this.regs[REG_PC] = (pc + 2) >>> 0;
      return this.completeThumbInstruction(false, false);
    },

    execThumbBlSuffix(pc, opcode) {
      const offset = opcode & 0x07ff;
      const nextLr = ((pc + 2) | 1) >>> 0;
      const newPc = (this.regs[REG_LR] + ((offset << 1) >>> 0)) >>> 0;
      this.regs[REG_LR] = nextLr;
      this.regs[REG_PC] = newPc;
      this.addNseqCycles(newPc, false);
      return this.completeThumbInstruction(true, false);
    },

    execThumbSwi(pc) {
      this.regs[REG_BUS_VALUE] = 0xe3a02004;
      this.regMode[(MODE_SUPERVISOR & 0x0f) * 7 + 6] = (pc + 2) >>> 0;
      this.spsr[MODE_SUPERVISOR & 0x0f] = this.regs[REG_CPSR] >>> 0;
      this.regs[REG_PC] = 0x00000008;
      this.applyCpsr(((this.regs[REG_CPSR] & ~0x3f) | 0x13 | 0x80) >>> 0, false);
      return this.completeThumbInstruction(true, true);
    },

    execThumbPcRelativeLoad(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const rd = (opcode >>> 8) & 0x07;
      const imm = (opcode & 0xff) << 2;
      const address = (((pc & ~2) + 4 + imm) >>> 0);
      this.regs[rd] = bridge.readMemory32(address) >>> 0;
      this.addNseqCycles(address, true);
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbRegOffsetTransfer(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const top = (opcode >>> 8) & 0xff;
      const ro = (opcode >>> 6) & 0x07;
      const rb = (opcode >>> 3) & 0x07;
      const rd = opcode & 0x07;
      const address = ((this.regs[rb] >>> 0) + (this.regs[ro] >>> 0)) >>> 0;

      switch (top & 0xfe) {
        case 0x50:
          this.writeMemory32(address, this.regs[rd] >>> 0);
          this.addNseqCycles(address, true);
          break;
        case 0x52:
          this.writeMemory16(address, this.regs[rd] >>> 0);
          this.addNseqCycles(address, false);
          break;
        case 0x54:
          this.writeMemory8(address, this.regs[rd] >>> 0);
          this.addNseqCycles(address, false);
          break;
        case 0x56:
          this.regs[rd] = this.readSigned8(address);
          this.addNseqCycles(address, false);
          break;
        case 0x58:
          this.regs[rd] = bridge.readMemory32(address) >>> 0;
          this.addNseqCycles(address, true);
          break;
        case 0x5a:
          this.regs[rd] = bridge.readMemory16(address) & 0xffff;
          this.addNseqCycles(address, false);
          break;
        case 0x5c:
          this.regs[rd] = bridge.readMemory8(address) & 0xff;
          this.addNseqCycles(address, false);
          break;
        case 0x5e:
          this.regs[rd] = this.readSigned16(address);
          this.addNseqCycles(address, false);
          break;
        default:
          throw new Error(`unsupported thumb reg-offset transfer 0x${opcode.toString(16)}`);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbImmOffsetTransfer(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const top = (opcode >>> 8) & 0xff;
      const imm = (opcode >>> 6) & 0x1f;
      const rb = (opcode >>> 3) & 0x07;
      const rd = opcode & 0x07;
      const base = this.regs[rb] >>> 0;
      let address;

      if (top <= 0x67) {
        address = (base + (imm << 2)) >>> 0;
        this.writeMemory32(address, this.regs[rd] >>> 0);
        this.addNseqCycles(address, true);
      } else if (top <= 0x6f) {
        address = (base + (imm << 2)) >>> 0;
        this.regs[rd] = bridge.readMemory32(address) >>> 0;
        this.addNseqCycles(address, true);
      } else if (top <= 0x77) {
        address = (base + imm) >>> 0;
        this.writeMemory8(address, this.regs[rd] >>> 0);
        this.addNseqCycles(address, false);
      } else if (top <= 0x7f) {
        address = (base + imm) >>> 0;
        this.regs[rd] = bridge.readMemory8(address) & 0xff;
        this.addNseqCycles(address, false);
      } else if (top <= 0x87) {
        address = (base + (imm << 1)) >>> 0;
        this.writeMemory16(address, this.regs[rd] >>> 0);
        this.addNseqCycles(address, false);
      } else {
        address = (base + (imm << 1)) >>> 0;
        this.regs[rd] = bridge.readMemory16(address) & 0xffff;
        this.addNseqCycles(address, false);
      }

      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbSpRelativeTransfer(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const top = (opcode >>> 8) & 0xff;
      const rd = (opcode >>> 8) & 0x07;
      const address = ((this.regs[REG_SP] >>> 0) + ((opcode & 0xff) << 2)) >>> 0;

      if (top <= 0x97) {
        this.writeMemory32(address, this.regs[rd] >>> 0);
      } else {
        this.regs[rd] = bridge.readMemory32(address) >>> 0;
      }

      this.addNseqCycles(address, true);
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbAddPcSp(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const rd = (opcode >>> 8) & 0x07;
      const imm = (opcode & 0xff) << 2;
      const top = (opcode >>> 8) & 0xff;
      const base = top <= 0xa7 ? (((pc & ~2) + 4) >>> 0) : (this.regs[REG_SP] >>> 0);
      this.regs[rd] = (base + imm) >>> 0;
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbAdjustSp(pc, opcode) {
      const nextPc = (pc + 2) >>> 0;
      const imm = (opcode & 0x7f) << 2;
      if ((opcode >>> 7) & 1) {
        this.regs[REG_SP] = ((this.regs[REG_SP] >>> 0) - imm) >>> 0;
      } else {
        this.regs[REG_SP] = ((this.regs[REG_SP] >>> 0) + imm) >>> 0;
      }
      this.regs[REG_PC] = nextPc;
      return this.completeThumbInstruction(false, false);
    },

    execThumbBlockMem(pc, load, rn, regList, preDec) {
      const nextPc = (pc + 2) >>> 0;
      const base = this.regs[rn] >>> 0;
      const count = popcount16(regList & 0xff) + ((regList & 0xff00) ? 1 : 0);
      const endAddr = preDec ? ((base - (count * 4)) >>> 0) : ((base + (count * 4)) >>> 0);
      let address = preDec ? endAddr : base;
      const baseMask = (1 << rn) >>> 0;
      const baseFirst = ((((1 << rn) >>> 0) - 1) & regList) === 0;
      const writeBackFirst = load || !((regList & baseMask) !== 0 && baseFirst);

      if (writeBackFirst) {
        this.regs[rn] = endAddr >>> 0;
      }

      this.regs[REG_PC] = nextPc;

      if (load) {
        for (let reg = 0; reg < 8; reg++) {
          if (((regList >>> reg) & 1) === 0) {
            continue;
          }
          this.regs[reg] = bridge.readMemory32(address) >>> 0;
          this.addSeqCycles(address, true);
          address = (address + 4) >>> 0;
        }

        if (regList & (1 << REG_PC)) {
          this.regs[REG_PC] = (bridge.readMemory32(address) & ~1) >>> 0;
          this.addSeqCycles(address, true);
          address = (address + 4) >>> 0;
        }
      } else {
        for (let reg = 0; reg < 8; reg++) {
          if (((regList >>> reg) & 1) === 0) {
            continue;
          }
          this.writeMemory32(address, this.regs[reg] >>> 0);
          this.addSeqCycles(address, true);
          address = (address + 4) >>> 0;
        }

        if (regList & (1 << REG_LR)) {
          this.writeMemory32(address, this.regs[REG_LR] >>> 0);
          this.addSeqCycles(address, true);
          address = (address + 4) >>> 0;
        }
      }

      if (!writeBackFirst) {
        this.regs[rn] = endAddr >>> 0;
      }

      if (!load || (regList & (1 << REG_PC)) === 0) {
        this.regs[REG_PC] = nextPc;
      }

      return this.completeThumbInstruction(true, false);
    },

    execThumbPushPop(pc, opcode) {
      const top = (opcode >>> 8) & 0xff;
      const load = top >= 0xbc;
      let regList = opcode & 0xff;
      if (top === 0xb5) regList |= 1 << REG_LR;
      if (top === 0xbd) regList |= 1 << REG_PC;
      return this.execThumbBlockMem(pc, load, REG_SP, regList, !load);
    },

    execThumbBlockTransfer(pc, opcode) {
      const top = (opcode >>> 8) & 0xff;
      const load = top >= 0xc8;
      const rn = (opcode >>> 8) & 0x07;
      return this.execThumbBlockMem(pc, load, rn, opcode & 0xff, false);
    },

    execThumbOpcode(pc, opcode) {
      const top = (opcode >>> 8) & 0xff;

      if (top <= 0x07) return this.execThumbShift(pc, opcode, 0);
      if (top <= 0x0f) return this.execThumbShift(pc, opcode, 1);
      if (top <= 0x17) return this.execThumbShift(pc, opcode, 2);
      if (top <= 0x19) return this.execThumbAddSub(pc, opcode, false, false);
      if (top <= 0x1b) return this.execThumbAddSub(pc, opcode, true, false);
      if (top <= 0x1d) return this.execThumbAddSub(pc, opcode, false, true);
      if (top <= 0x1f) return this.execThumbAddSub(pc, opcode, true, true);
      if (top <= 0x27) return this.execThumbImmOp(pc, opcode, "mov");
      if (top <= 0x2f) return this.execThumbImmOp(pc, opcode, "cmp");
      if (top <= 0x37) return this.execThumbImmOp(pc, opcode, "add");
      if (top <= 0x3f) return this.execThumbImmOp(pc, opcode, "sub");
      if (top >= 0x40 && top <= 0x43) return this.execThumbAlu(pc, opcode);
      if (top >= 0x44 && top <= 0x47) return this.execThumbHiReg(pc, opcode);
      if (top >= 0x48 && top <= 0x4f) return this.execThumbPcRelativeLoad(pc, opcode);
      if (top >= 0x50 && top <= 0x5f) return this.execThumbRegOffsetTransfer(pc, opcode);
      if (top >= 0x60 && top <= 0x8f) return this.execThumbImmOffsetTransfer(pc, opcode);
      if (top >= 0x90 && top <= 0x9f) return this.execThumbSpRelativeTransfer(pc, opcode);
      if (top >= 0xa0 && top <= 0xaf) return this.execThumbAddPcSp(pc, opcode);
      if (top >= 0xb0 && top <= 0xb3) return this.execThumbAdjustSp(pc, opcode);
      if (top === 0xb4 || top === 0xb5 || top === 0xbc || top === 0xbd) return this.execThumbPushPop(pc, opcode);
      if (top >= 0xc0 && top <= 0xcf) return this.execThumbBlockTransfer(pc, opcode);
      if (top >= 0xd0 && top <= 0xdd) return this.execThumbCondBranch(pc, opcode);
      if (top === 0xdf) return this.execThumbSwi(pc);
      if (top >= 0xe0 && top <= 0xe7) return this.execThumbBranch(pc, opcode);
      if (top >= 0xf0 && top <= 0xf7) return this.execThumbBlPrefix(pc, opcode);
      if (top >= 0xf8 && top <= 0xff) return this.execThumbBlSuffix(pc, opcode);

      throw new Error(`unsupported Thumb opcode 0x${(opcode & 0xffff).toString(16)}`);
    },

    completeArmInstruction(forceDispatch, skipSeq) {
      if (!skipSeq) {
        this.addSeqCycles(this.regs[REG_PC] >>> 0, true);
      }

      if ((bridge.idleLoopTargetPc[0] >>> 0) === (this.regs[REG_PC] >>> 0) && this.cyclesRemaining > 0) {
        this.cyclesRemaining = 0;
      }

      if (forceDispatch) {
        return 1;
      }
      if (this.cyclesRemaining <= 0) {
        return 1;
      }
      if ((this.regs[CPU_HALT_STATE] >>> 0) !== CPU_ACTIVE) {
        return 1;
      }
      if (this.pendingAlert & (CPU_ALERT_HALT | CPU_ALERT_IRQ | CPU_ALERT_SMC)) {
        return 1;
      }
      if ((this.regs[REG_CPSR] & THUMB_BIT) !== 0) {
        return 1;
      }
      return 0;
    },

    completeThumbInstruction(forceDispatch, skipSeq) {
      if (!skipSeq) {
        this.addSeqCycles(this.regs[REG_PC] >>> 0, false);
      }

      if ((bridge.idleLoopTargetPc[0] >>> 0) === (this.regs[REG_PC] >>> 0) && this.cyclesRemaining > 0) {
        this.cyclesRemaining = 0;
      }

      if (forceDispatch) {
        return 1;
      }
      if (this.cyclesRemaining <= 0) {
        return 1;
      }
      if ((this.regs[CPU_HALT_STATE] >>> 0) !== CPU_ACTIVE) {
        return 1;
      }
      if (this.pendingAlert & (CPU_ALERT_HALT | CPU_ALERT_IRQ | CPU_ALERT_SMC)) {
        return 1;
      }
      if ((this.regs[REG_CPSR] & THUMB_BIT) === 0) {
        return 1;
      }
      return 0;
    },

    isArmPsrOpcode(opcode) {
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
    },

    armWritesPc(opcode) {
      if ((opcode & 0x0c000000) !== 0) {
        return false;
      }

      const op = (opcode >>> 21) & 0xf;
      const rd = (opcode >>> 12) & 0xf;
      return rd === REG_PC && (op < 0x8 || op >= 0xc);
    },

    armLoadWritesPc(opcode) {
      return ((opcode & 0x0c100000) === 0x04100000) && (((opcode >>> 12) & 0xf) === REG_PC);
    },

    shouldTerminateArmOpcode(opcode, nextPc) {
      if ((opcode & 0x0ffffff0) === 0x012fff10) {
        return true;
      }
      if ((opcode & 0x0f000000) === 0x0f000000) {
        return true;
      }
      if ((opcode & 0x0e000000) === 0x0a000000) {
        return true;
      }
      if ((opcode & 0x0e000000) === 0x08000000) {
        return true;
      }
      if (this.armLoadWritesPc(opcode)) {
        return true;
      }
      if ((opcode & 0x0c000000) === 0x00000000) {
        if ((opcode & 0x02000090) === 0x00000090) {
          return true;
        }
        if (this.isArmPsrOpcode(opcode) || this.armWritesPc(opcode)) {
          return true;
        }
      }
      return this.isTranslationGateTarget(nextPc);
    },

    shouldTerminateThumbOpcode(opcode) {
      const top = (opcode >>> 8) & 0xff;

      if (top === 0x47 || top === 0xdf) {
        return true;
      }
      if (top === 0xb4 || top === 0xb5 || top === 0xbc || top === 0xbd) {
        return true;
      }
      if (top >= 0xc0 && top <= 0xcf) {
        return true;
      }
      if ((top >= 0xd0 && top <= 0xdf) || (top >= 0xe0 && top <= 0xe7) || top >= 0xf8) {
        return true;
      }
      if ((top === 0x44 || top === 0x46) && ((((opcode >>> 4) & 0x08) | (opcode & 0x07)) === REG_PC)) {
        return true;
      }

      return false;
    },

    buildBlockExecutor(block) {
      this.ensureBlockFactories();
      if (block.thumb) {
        return buildThumbBlockExecutor(block, this.thumbBlockFactory);
      }

      return buildArmBlockExecutor(block, this.armBlockFactory);
    },

    compileArmBlock(entryPc) {
      const steps = [];
      let pc = entryPc >>> 0;

      for (let i = 0; i < this.maxArmBlockInstructions; i++) {
        const opcode = this.fetchArmOpcode(pc) >>> 0;
        const nextPc = (pc + 4) >>> 0;
        steps.push({ pc, opcode });
        if (this.shouldTerminateArmOpcode(opcode, nextPc)) {
          break;
        }
        pc = nextPc;
      }

      const block = {
        entryPc: entryPc >>> 0,
        thumb: false,
        idle: (bridge.idleLoopTargetPc[0] >>> 0) === (entryPc >>> 0),
        mutable: steps.some((step) => this.isMutableCodeRegion(step.pc)),
        steps,
      };

      block.execute = this.buildBlockExecutor(block);
      return block;
    },

    compileThumbBlock(entryPc) {
      const steps = [];
      let pc = entryPc >>> 0;

      for (let i = 0; i < this.maxThumbBlockInstructions; i++) {
        const opcode = this.fetchThumbOpcode(pc) & 0xffff;
        const nextPc = (pc + 2) >>> 0;
        steps.push({ pc, opcode });
        if (this.shouldTerminateThumbOpcode(opcode)) {
          break;
        }
        if (this.isTranslationGateTarget(nextPc)) {
          break;
        }
        pc = nextPc;
      }

      const block = {
        entryPc: entryPc >>> 0,
        thumb: true,
        idle: (bridge.idleLoopTargetPc[0] >>> 0) === (entryPc >>> 0),
        mutable: steps.some((step) => this.isMutableCodeRegion(step.pc)),
        steps,
      };

      block.execute = this.buildBlockExecutor(block);
      return block;
    },

    getBlock(pc, thumb) {
      const cache = thumb ? this.thumbBlocks : this.armBlocks;
      let block = cache.get(pc >>> 0);

      if (block && !this.validateBlock(block)) {
        cache.delete(pc >>> 0);
        block = null;
      }

      if (!block) {
        block = thumb ? this.compileThumbBlock(pc) : this.compileArmBlock(pc);
        cache.set(pc >>> 0, block);
      }

      return block;
    },

    runScheduler() {
      const ret = bridge.updateGba(this.cyclesRemaining | 0) >>> 0;
      this.pendingAlert = 0;
      this.cyclesRemaining = (ret & 0x7fff) | 0;
      return (ret & 0x80000000) !== 0;
    },

    dispatchOneBlock() {
      const pc = this.regs[REG_PC] >>> 0;
      const thumb = (this.regs[REG_CPSR] & THUMB_BIT) !== 0;
      try {
        const block = this.getBlock(pc, thumb);
        const result = block.execute(this.regs) >>> 0;
        this.cyclesRemaining -= result;
        if ((bridge.idleLoopTargetPc[0] >>> 0) === (this.regs[REG_PC] >>> 0) && this.cyclesRemaining > 0) {
          this.cyclesRemaining = 0;
        }
        this.processPendingAlert();
        bridge.checkAndRaiseInterrupts();
        return 1;
      } catch (error) {
        throw error;
      }
    },

    execute(cycles) {
      this.ensureViews();
      this.stats.executions++;
      this.cyclesRemaining = cycles | 0;

      for (;;) {
        if ((this.regs[REG_PC] >>> 0) === 0 && this.cyclesRemaining === 0) {
          return;
        }

        if (this.regs[CPU_MODE] === undefined) {
          throw new Error("invalid CPU register view");
        }

        if ((this.regs[CPU_HALT_STATE] >>> 0) !== CPU_ACTIVE) {
          if (this.runScheduler()) {
            return;
          }
          continue;
        }

        if (this.cyclesRemaining <= 0) {
          if (this.runScheduler()) {
            return;
          }
          continue;
        }

        this.pendingAlert = 0;
        this.dispatchOneBlock();
      }
    },
  };

  return runtime.reset();
}

module.exports = {
  createGpspJsJitRuntime,
};
