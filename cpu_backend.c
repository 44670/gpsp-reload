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
#include "cpu_backend.h"
#include "jsjit_backend.h"

static bool cpu_backend_force_jsjit;

cpu_backend_type cpu_backend_current(void)
{
  if (cpu_backend_force_jsjit)
    return CPU_BACKEND_JSJIT;

#ifdef HAVE_DYNAREC
  if (dynarec_enable)
    return CPU_BACKEND_DYNAREC;
#endif
  return CPU_BACKEND_INTERP;
}

const char *cpu_backend_name(cpu_backend_type backend)
{
  switch (backend)
  {
    case CPU_BACKEND_INTERP:
      return "interp";
    case CPU_BACKEND_DYNAREC:
      return "dynarec";
    case CPU_BACKEND_JSJIT:
      return "jsjit";
  }

  return "unknown";
}

void cpu_backend_set_jsjit(bool enable)
{
  cpu_backend_force_jsjit = enable;
}

bool cpu_backend_jsjit_enabled(void)
{
  return cpu_backend_force_jsjit;
}

void cpu_backend_reset(void)
{
  if (cpu_backend_current() == CPU_BACKEND_JSJIT)
  {
    jsjit_backend_reset();
    return;
  }

#ifdef HAVE_DYNAREC
  /*
   * Native dynarec initialization historically happened on every reset,
   * even when the runtime option was temporarily disabled. Keep that
   * behavior so toggling the frontend option does not leave the emitter
   * uninitialized.
   */
  init_dynarec_caches();
  init_emitter(gamepak_must_swap());
#endif
}

void cpu_backend_execute(u32 cycles)
{
  switch (cpu_backend_current())
  {
#ifdef HAVE_DYNAREC
    case CPU_BACKEND_DYNAREC:
      execute_arm_translate(cycles);
      return;
#endif
    case CPU_BACKEND_INTERP:
      /* Sticky bits are only used by the interpreter path today. */
      clear_gamepak_stickybits();
      execute_arm(cycles);
      return;
    case CPU_BACKEND_JSJIT:
      jsjit_backend_execute(cycles);
      return;
    default:
      clear_gamepak_stickybits();
      execute_arm(cycles);
      return;
  }
}
