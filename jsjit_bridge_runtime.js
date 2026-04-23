"use strict";

const { hex32 } = require("./jsjit_common.js");

function bindModuleFunction(Module, name, ret, args) {
  const direct = Module[`_${name}`];
  if (typeof direct === "function") {
    return (...params) => direct(...params);
  }

  if (typeof Module.cwrap === "function") {
    return Module.cwrap(name, ret, args || []);
  }

  throw new Error(`missing Emscripten export ${name}`);
}

function createGpspJsJitBridge(Module) {
  if (!Module) {
    throw new Error("createGpspJsJitBridge requires an Emscripten Module");
  }

  const bind = (name, ret, args) => bindModuleFunction(Module, name, ret, args);

  const getters = {
    pointerSize: bind("jsjit_bridge_pointer_size", "number"),
    regPtr: bind("jsjit_bridge_reg_ptr", "number"),
    spsrPtr: bind("jsjit_bridge_spsr_ptr", "number"),
    regModePtr: bind("jsjit_bridge_reg_mode_ptr", "number"),
    memoryMapReadPtr: bind("jsjit_bridge_memory_map_read_ptr", "number"),
    biosRomPtr: bind("jsjit_bridge_bios_rom_ptr", "number"),
    openBiosRomPtr: bind("jsjit_bridge_open_bios_rom_ptr", "number"),
    ewramPtr: bind("jsjit_bridge_ewram_ptr", "number"),
    ewramShadowPtr: bind("jsjit_bridge_ewram_shadow_ptr", "number"),
    iwramPtr: bind("jsjit_bridge_iwram_ptr", "number"),
    iwramDataPtr: bind("jsjit_bridge_iwram_data_ptr", "number"),
    iwramShadowPtr: bind("jsjit_bridge_iwram_shadow_ptr", "number"),
    vramPtr: bind("jsjit_bridge_vram_ptr", "number"),
    paletteRamPtr: bind("jsjit_bridge_palette_ram_ptr", "number"),
    oamRamPtr: bind("jsjit_bridge_oam_ram_ptr", "number"),
    ioRegistersPtr: bind("jsjit_bridge_io_registers_ptr", "number"),
    executeCyclesPtr: bind("jsjit_bridge_execute_cycles_ptr", "number"),
    cpuTicksPtr: bind("jsjit_bridge_cpu_ticks_ptr", "number"),
    frameCounterPtr: bind("jsjit_bridge_frame_counter_ptr", "number"),
    wsCycSeqPtr: bind("jsjit_bridge_ws_cyc_seq_ptr", "number"),
    wsCycNseqPtr: bind("jsjit_bridge_ws_cyc_nseq_ptr", "number"),
    idleLoopTargetPcPtr: bind("jsjit_bridge_idle_loop_target_pc_ptr", "number"),
    translationGateTargetsPtr: bind("jsjit_bridge_translation_gate_targets_ptr", "number"),
    translationGateTargetPcPtr: bind("jsjit_bridge_translation_gate_target_pc_ptr", "number"),
    regWordCount: bind("jsjit_bridge_reg_word_count", "number"),
    regArchCount: bind("jsjit_bridge_reg_arch_count", "number"),
    spsrWordCount: bind("jsjit_bridge_spsr_word_count", "number"),
    regModeWordCount: bind("jsjit_bridge_reg_mode_word_count", "number"),
    memoryMapReadCount: bind("jsjit_bridge_memory_map_read_count", "number"),
    biosRomBytes: bind("jsjit_bridge_bios_rom_bytes", "number"),
    openBiosRomBytes: bind("jsjit_bridge_open_bios_rom_bytes", "number"),
    ewramBytes: bind("jsjit_bridge_ewram_bytes", "number"),
    ewramDataBytes: bind("jsjit_bridge_ewram_data_bytes", "number"),
    ewramShadowOffset: bind("jsjit_bridge_ewram_shadow_offset", "number"),
    iwramBytes: bind("jsjit_bridge_iwram_bytes", "number"),
    iwramDataBytes: bind("jsjit_bridge_iwram_data_bytes", "number"),
    iwramDataOffset: bind("jsjit_bridge_iwram_data_offset", "number"),
    vramBytes: bind("jsjit_bridge_vram_bytes", "number"),
    paletteRamBytes: bind("jsjit_bridge_palette_ram_bytes", "number"),
    oamRamBytes: bind("jsjit_bridge_oam_ram_bytes", "number"),
    ioRegistersBytes: bind("jsjit_bridge_io_registers_bytes", "number"),
    wsTableBytes: bind("jsjit_bridge_ws_table_bytes", "number"),
    pageShift: bind("jsjit_bridge_page_shift", "number"),
    pageSize: bind("jsjit_bridge_page_size", "number"),
    translationGateCapacity: bind("jsjit_bridge_translation_gate_capacity", "number"),
  };

  const bridge = {
    Module,

    getHeapViews() {
      if (typeof Module.__gpspGetHeapViews === "function") {
        return Module.__gpspGetHeapViews();
      }
      return {
        HEAPU8: Module.HEAPU8,
        HEAPU16: Module.HEAPU16,
        HEAPU32: Module.HEAPU32,
      };
    },

    refreshPointers() {
      this.pointerSize = getters.pointerSize();
      if (this.pointerSize !== 4) {
        throw new Error(`unsupported pointer size ${this.pointerSize}`);
      }

      this.regPtr = getters.regPtr();
      this.spsrPtr = getters.spsrPtr();
      this.regModePtr = getters.regModePtr();
      this.memoryMapReadPtr = getters.memoryMapReadPtr();
      this.biosRomPtr = getters.biosRomPtr();
      this.openBiosRomPtr = getters.openBiosRomPtr();
      this.ewramPtr = getters.ewramPtr();
      this.ewramShadowPtr = getters.ewramShadowPtr();
      this.iwramPtr = getters.iwramPtr();
      this.iwramDataPtr = getters.iwramDataPtr();
      this.iwramShadowPtr = getters.iwramShadowPtr();
      this.vramPtr = getters.vramPtr();
      this.paletteRamPtr = getters.paletteRamPtr();
      this.oamRamPtr = getters.oamRamPtr();
      this.ioRegistersPtr = getters.ioRegistersPtr();
      this.executeCyclesPtr = getters.executeCyclesPtr();
      this.cpuTicksPtr = getters.cpuTicksPtr();
      this.frameCounterPtr = getters.frameCounterPtr();
      this.wsCycSeqPtr = getters.wsCycSeqPtr();
      this.wsCycNseqPtr = getters.wsCycNseqPtr();
      this.idleLoopTargetPcPtr = getters.idleLoopTargetPcPtr();
      this.translationGateTargetsPtr = getters.translationGateTargetsPtr();
      this.translationGateTargetPcPtr = getters.translationGateTargetPcPtr();

      this.regWordCount = getters.regWordCount();
      this.regArchCount = getters.regArchCount();
      this.spsrWordCount = getters.spsrWordCount();
      this.regModeWordCount = getters.regModeWordCount();
      this.memoryMapReadCount = getters.memoryMapReadCount();
      this.biosRomBytes = getters.biosRomBytes();
      this.openBiosRomBytes = getters.openBiosRomBytes();
      this.ewramBytes = getters.ewramBytes();
      this.ewramDataBytes = getters.ewramDataBytes();
      this.ewramShadowOffset = getters.ewramShadowOffset();
      this.iwramBytes = getters.iwramBytes();
      this.iwramDataBytes = getters.iwramDataBytes();
      this.iwramDataOffset = getters.iwramDataOffset();
      this.vramBytes = getters.vramBytes();
      this.paletteRamBytes = getters.paletteRamBytes();
      this.oamRamBytes = getters.oamRamBytes();
      this.ioRegistersBytes = getters.ioRegistersBytes();
      this.wsTableBytes = getters.wsTableBytes();
      this.pageShift = getters.pageShift();
      this.pageSize = getters.pageSize();
      this.translationGateCapacity = getters.translationGateCapacity();
    },

    refreshViews() {
      this.refreshPointers();

      const heaps = this.getHeapViews();
      if (!heaps || !heaps.HEAPU8 || !heaps.HEAPU16 || !heaps.HEAPU32) {
        throw new Error("jsjit bridge could not access Emscripten heap views");
      }

      const buffer = heaps.HEAPU8.buffer;
      this.heapU8 = heaps.HEAPU8;
      this.heapU16 = heaps.HEAPU16;
      this.heapU32 = heaps.HEAPU32;

      this.regs = new Uint32Array(buffer, this.regPtr, this.regWordCount);
      this.spsr = new Uint32Array(buffer, this.spsrPtr, this.spsrWordCount);
      this.regMode = new Uint32Array(buffer, this.regModePtr, this.regModeWordCount);
      this.memoryMapRead = new Uint32Array(buffer, this.memoryMapReadPtr, this.memoryMapReadCount);
      this.biosRom = new Uint8Array(buffer, this.biosRomPtr, this.biosRomBytes);
      this.openBiosRom = new Uint8Array(buffer, this.openBiosRomPtr, this.openBiosRomBytes);
      this.ewram = new Uint8Array(buffer, this.ewramPtr, this.ewramBytes);
      this.iwram = new Uint8Array(buffer, this.iwramPtr, this.iwramBytes);
      this.vram = new Uint8Array(buffer, this.vramPtr, this.vramBytes);
      this.paletteRam = new Uint16Array(buffer, this.paletteRamPtr, this.paletteRamBytes >>> 1);
      this.oamRam = new Uint16Array(buffer, this.oamRamPtr, this.oamRamBytes >>> 1);
      this.ioRegisters = new Uint16Array(buffer, this.ioRegistersPtr, this.ioRegistersBytes >>> 1);
      this.executeCycles = new Uint32Array(buffer, this.executeCyclesPtr, 1);
      this.cpuTicks = new Uint32Array(buffer, this.cpuTicksPtr, 1);
      this.frameCounter = new Uint32Array(buffer, this.frameCounterPtr, 1);
      this.wsCycSeq = new Uint8Array(buffer, this.wsCycSeqPtr, this.wsTableBytes);
      this.wsCycNseq = new Uint8Array(buffer, this.wsCycNseqPtr, this.wsTableBytes);
      this.idleLoopTargetPc = new Uint32Array(buffer, this.idleLoopTargetPcPtr, 1);
      this.translationGateTargets = new Uint32Array(buffer, this.translationGateTargetsPtr, 1);
      this.translationGateTargetPc = new Uint32Array(
        buffer,
        this.translationGateTargetPcPtr,
        this.translationGateCapacity
      );
      return this;
    },

    snapshotCpu() {
      return {
        pc: this.regs[15] >>> 0,
        sp: this.regs[13] >>> 0,
        lr: this.regs[14] >>> 0,
        cpsr: this.regs[16] >>> 0,
        mode: this.regs[17] >>> 0,
        haltState: this.regs[18] >>> 0,
        executeCycles: this.executeCycles[0] >>> 0,
        cpuTicks: this.cpuTicks[0] >>> 0,
        frameCounter: this.frameCounter[0] >>> 0,
      };
    },

    snapshotMemoryLayout() {
      return {
        pageShift: this.pageShift >>> 0,
        pageSize: this.pageSize >>> 0,
        memoryMapReadPtr: this.memoryMapReadPtr >>> 0,
        ewram: {
          base: this.ewramPtr >>> 0,
          bytes: this.ewramBytes >>> 0,
          dataBytes: this.ewramDataBytes >>> 0,
          shadowOffset: this.ewramShadowOffset >>> 0,
          shadowBase: this.ewramShadowPtr >>> 0,
        },
        iwram: {
          base: this.iwramPtr >>> 0,
          bytes: this.iwramBytes >>> 0,
          dataBase: this.iwramDataPtr >>> 0,
          dataBytes: this.iwramDataBytes >>> 0,
          dataOffset: this.iwramDataOffset >>> 0,
          shadowBase: this.iwramShadowPtr >>> 0,
        },
        vram: {
          base: this.vramPtr >>> 0,
          bytes: this.vramBytes >>> 0,
        },
        ioRegisters: {
          base: this.ioRegistersPtr >>> 0,
          bytes: this.ioRegistersBytes >>> 0,
        },
      };
    },

    currentPageBase(address) {
      return this.memoryMapRead[address >>> this.pageShift] >>> 0;
    },

    describeState() {
      const cpu = this.snapshotCpu();
      const pageBase = this.currentPageBase(cpu.pc);
      return (
        `pc=${hex32(cpu.pc)} cpsr=${hex32(cpu.cpsr)} mode=${hex32(cpu.mode)} ` +
        `halt=${cpu.haltState} exec=${cpu.executeCycles} page=${hex32(pageBase)}`
      );
    },

    printState(log) {
      const printer = log || console.log;
      printer(this.describeState());
    },
  };

  bridge.readMemory8 = bind("jsjit_bridge_read_memory8", "number", ["number"]);
  bridge.readMemory16 = bind("jsjit_bridge_read_memory16", "number", ["number"]);
  bridge.readMemory32 = bind("jsjit_bridge_read_memory32", "number", ["number"]);
  bridge.writeMemory8 = bind("jsjit_bridge_write_memory8", "number", ["number", "number"]);
  bridge.writeMemory16 = bind("jsjit_bridge_write_memory16", "number", ["number", "number"]);
  bridge.writeMemory32 = bind("jsjit_bridge_write_memory32", "number", ["number", "number"]);
  bridge.takePendingAlert = bind("jsjit_bridge_take_pending_alert", "number", []);
  bridge.updateGba = bind("jsjit_bridge_update_gba", "number", ["number"]);
  bridge.checkAndRaiseInterrupts = bind("jsjit_bridge_check_and_raise_interrupts", "number", []);
  bridge.flagInterrupt = bind("jsjit_bridge_flag_interrupt", "number", ["number"]);
  bridge.setCpuMode = bind("jsjit_bridge_set_cpu_mode", null, ["number"]);
  bridge.executeArm = bind("jsjit_bridge_execute_arm", null, ["number"]);
  bridge.loadGamepakPage = bind("jsjit_bridge_load_gamepak_page", "number", ["number"]);

  return bridge.refreshViews();
}

module.exports = {
  createGpspJsJitBridge,
};
