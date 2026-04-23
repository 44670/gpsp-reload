"use strict";

(function() {
  if (typeof require === "undefined") {
    return;
  }

  const path = require("path");
  const jsjit = require(path.resolve(__dirname, "../../jsjit_runtime.js"));

  Module.__gpspGetHeapViews = function() {
    return {
      HEAPU8,
      HEAPU16,
      HEAPU32,
    };
  };

  globalThis.__gpspCreateGpspJsJit = jsjit.createGpspJsJitRuntime;
})();
