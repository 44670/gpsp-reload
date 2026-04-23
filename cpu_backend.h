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

#ifndef CPU_BACKEND_H
#define CPU_BACKEND_H

#include "common.h"

typedef enum
{
  CPU_BACKEND_INTERP = 0,
  CPU_BACKEND_DYNAREC = 1,
  CPU_BACKEND_JSJIT = 2
} cpu_backend_type;

cpu_backend_type cpu_backend_current(void);
const char *cpu_backend_name(cpu_backend_type backend);
void cpu_backend_set_jsjit(bool enable);
bool cpu_backend_jsjit_enabled(void);
void cpu_backend_reset(void);
void cpu_backend_execute(u32 cycles);

#endif
