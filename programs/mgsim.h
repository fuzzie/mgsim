/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011  The Microgrid Project.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#ifndef MGSIM_H
#define MGSIM_H

/*
 * This header collects "hard" constants that are decided by
 * convention and must be known from both hardware and software.
 */


/**** Ancillary processor registers ****/

#if defined(__mtalpha__)
#define mgsim_read_asr(DestVar, Register) __asm__ __volatile__("getasr %1, %0" : "=r"(DestVar) : "I"(Register))
#define mgsim_read_apr(DestVar, Register) __asm__ __volatile__("getapr %1, %0" : "=r"(DestVar) : "I"(Register))
#endif
#if defined(__mtsparc__)
/* on MT-SPARC, ASR0 is %y, ASR15 is STBAR, and ASR20 are Microthreaded opcodes. */
#define mgsim_read_asr(DestVar, Register) __asm__ __volatile__("rd %%asr%1, %0" : "=r"(DestVar) : "I"((Register)+1))
#define mgsim_read_apr(DestVar, Register) __asm__ __volatile__("rd %%asr%1, %0" : "=r"(DestVar) : "I"((Register)+21))
#endif

/* Ancillary system registers */

#define ASR_SYSTEM_VERSION    0
#define ASR_CONFIG_CAPS       1
#define ASR_DELEGATE_CAPS     2
#define ASR_SYSCALL_BASE      3
#define ASR_NUM_PERFCOUNTERS  4
#define ASR_PERFCOUNTERS_BASE 5
#define ASR_IO_PARAMS1        6
#define ASR_IO_PARAMS2        7
#define ASR_AIO_BASE          8
#define ASR_PNC_BASE          9
#define NUM_ASRS              10

/* value for ASR_SYSTEM_VERSION: to be updated whenever the list of ASRs
   changes */
#define ASR_SYSTEM_VERSION_VALUE  1

/*
// Suggested:
// #define APR_SEP                0
// #define APR_TLSTACK_FIRST_TOP  1
// #define APR_TLHEAP_BASE        2
// #define APR_TLHEAP_SIZE        3
// #define APR_GLHEAP             4
*/



/**** Configuration data ****/

/* the following constants refer to the configuration blocks when an
   ActiveROM is configured with source "CONFIG". */

/* the first word of the configuration area */
#define CONF_MAGIC    0x53474d23

/* the first word of each configuration block, that determines its
   structure. */
#define CONF_TAG_TYPETABLE 1
#define CONF_TAG_ATTRTABLE 2
#define CONF_TAG_RAWCONFIG 3
#define CONF_TAG_OBJECT    4
#define CONF_TAG_SYMBOL    5

/* the first word of a "value" entry for object and relation
   properties. */
#define CONF_ENTITY_VOID   1
#define CONF_ENTITY_SYMBOL 2
#define CONF_ENTITY_OBJECT 3
#define CONF_ENTITY_UINT   4


#endif
