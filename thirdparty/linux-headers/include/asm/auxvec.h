/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_AUXVEC_H
#define _ASM_X86_AUXVEC_H

#ifdef __i386__
#define AT_SYSINFO		32
#endif
#define AT_SYSINFO_EHDR		33

#if defined(CONFIG_IA32_EMULATION) || !defined(CONFIG_X86_64)
# define AT_VECTOR_SIZE_ARCH 3
#else
# define AT_VECTOR_SIZE_ARCH 2
#endif

#endif
