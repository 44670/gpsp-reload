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

#ifndef JSJIT_BACKEND_H
#define JSJIT_BACKEND_H

#include "common.h"

void jsjit_backend_reset(void);
void jsjit_backend_execute(u32 cycles);
u32 jsjit_backend_stat_executions(void);
u32 jsjit_backend_stat_fallback_executions(void);
u32 jsjit_backend_stat_fallback_cycles(void);

#endif
