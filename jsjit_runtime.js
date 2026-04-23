"use strict";

const common = require("./jsjit_common.js");
const { createGpspJsJitBridge } = require("./jsjit_bridge_runtime.js");
const { createGpspJsJitRuntime } = require("./jsjit_cpu_runtime.js");

module.exports = {
  ...common,
  createGpspJsJitBridge,
  createGpspJsJitRuntime,
  hex32: common.hex32,
};

if (typeof globalThis !== "undefined") {
  globalThis.createGpspJsJitBridge = createGpspJsJitBridge;
  globalThis.createGpspJsJitRuntime = createGpspJsJitRuntime;
}
