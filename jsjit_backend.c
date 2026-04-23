/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"
#include "cpu.h"
#include "jsjit_backend.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

EM_JS(void, jsjit_backend_reset_js, (), {
  if (typeof globalThis.__gpspCreateGpspJsJit !== "function")
    return;

  if (!globalThis.__gpspJsJitRuntime)
    globalThis.__gpspJsJitRuntime = globalThis.__gpspCreateGpspJsJit(Module);

  if (globalThis.__gpspJsJitRuntime && globalThis.__gpspJsJitRuntime.reset)
    globalThis.__gpspJsJitRuntime.reset();
});

EM_JS(void, jsjit_backend_execute_js, (u32 cycles), {
  if (typeof globalThis.__gpspCreateGpspJsJit !== "function") {
    Module._jsjit_bridge_execute_arm(cycles);
    return;
  }

  if (!globalThis.__gpspJsJitRuntime)
    globalThis.__gpspJsJitRuntime = globalThis.__gpspCreateGpspJsJit(Module);

  if (globalThis.__gpspJsJitRuntime &&
      typeof globalThis.__gpspJsJitRuntime.execute === "function") {
    globalThis.__gpspJsJitRuntime.execute(cycles >>> 0);
    return;
  }

  Module._jsjit_bridge_execute_arm(cycles);
});

EM_JS(u32, jsjit_backend_stat_executions_js, (), {
  if (!globalThis.__gpspJsJitRuntime || !globalThis.__gpspJsJitRuntime.stats)
    return 0;
  return globalThis.__gpspJsJitRuntime.stats.executions >>> 0;
});

EM_JS(u32, jsjit_backend_stat_fallback_executions_js, (), {
  if (!globalThis.__gpspJsJitRuntime || !globalThis.__gpspJsJitRuntime.stats)
    return 0;
  return globalThis.__gpspJsJitRuntime.stats.fallbackExecutions >>> 0;
});

EM_JS(u32, jsjit_backend_stat_fallback_cycles_js, (), {
  if (!globalThis.__gpspJsJitRuntime || !globalThis.__gpspJsJitRuntime.stats)
    return 0;
  return globalThis.__gpspJsJitRuntime.stats.fallbackCycles >>> 0;
});

#else

static u32 jsjit_exec_count;
static u32 jsjit_fallback_exec_count;
static u32 jsjit_fallback_cycle_count;

#endif

void jsjit_backend_reset(void)
{
#if defined(__EMSCRIPTEN__)
  jsjit_backend_reset_js();
#else
  jsjit_exec_count = 0;
  jsjit_fallback_exec_count = 0;
  jsjit_fallback_cycle_count = 0;
#endif
}

void jsjit_backend_execute(u32 cycles)
{
#if defined(__EMSCRIPTEN__)
  jsjit_backend_execute_js(cycles);
#else
  jsjit_exec_count++;
  jsjit_fallback_exec_count++;
  jsjit_fallback_cycle_count += cycles;
  clear_gamepak_stickybits();
  execute_arm(cycles);
#endif
}

u32 jsjit_backend_stat_executions(void)
{
#if defined(__EMSCRIPTEN__)
  return jsjit_backend_stat_executions_js();
#else
  return jsjit_exec_count;
#endif
}

u32 jsjit_backend_stat_fallback_executions(void)
{
#if defined(__EMSCRIPTEN__)
  return jsjit_backend_stat_fallback_executions_js();
#else
  return jsjit_fallback_exec_count;
#endif
}

u32 jsjit_backend_stat_fallback_cycles(void)
{
#if defined(__EMSCRIPTEN__)
  return jsjit_backend_stat_fallback_cycles_js();
#else
  return jsjit_fallback_cycle_count;
#endif
}
