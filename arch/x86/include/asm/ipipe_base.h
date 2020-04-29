/*   -*- linux-c -*-
 *   arch/x86/include/asm/ipipe_base.h
 *
 *   Copyright (C) 2007-2012 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __X86_IPIPE_BASE_H
#define __X86_IPIPE_BASE_H

#include <asm/irq_vectors.h>
#include <asm/bitsperlong.h>

#ifdef CONFIG_X86_32
/* 32 from IDT + iret_error + mayday trap */
#define IPIPE_TRAP_MAYDAY	33	/* Internal recovery trap */
#define IPIPE_NR_FAULTS		34
#else
/* 32 from IDT + mayday trap */
#define IPIPE_TRAP_MAYDAY	32	/* Internal recovery trap */
#define IPIPE_NR_FAULTS		33
#endif

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * Special APIC interrupts are mapped above the last defined external
 * IRQ number.
 */
#define nr_apic_vectors	        (NR_VECTORS - FIRST_SYSTEM_VECTOR)
#define IPIPE_FIRST_APIC_IRQ	NR_IRQS
#define IPIPE_HRTIMER_IPI	ipipe_apic_vector_irq(IPIPE_HRTIMER_VECTOR)
#ifdef CONFIG_SMP
#define IPIPE_RESCHEDULE_IPI	ipipe_apic_vector_irq(IPIPE_RESCHEDULE_VECTOR)
#define IPIPE_CRITICAL_IPI	ipipe_apic_vector_irq(IPIPE_CRITICAL_VECTOR)
#endif /* CONFIG_SMP */
#define IPIPE_NR_XIRQS		(NR_IRQS + nr_apic_vectors)
#define ipipe_apic_irq_vector(irq)  ((irq) - IPIPE_FIRST_APIC_IRQ + FIRST_SYSTEM_VECTOR)
#define ipipe_apic_vector_irq(vec)  ((vec) - FIRST_SYSTEM_VECTOR + IPIPE_FIRST_APIC_IRQ)
#else
#define IPIPE_NR_XIRQS		NR_IRQS
#endif /* !CONFIG_X86_LOCAL_APIC */

#ifndef __ASSEMBLY__

#include <asm/apicdef.h>

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

static inline const char *ipipe_clock_name(void)
{
	return "tsc";
}

#define __ipipe_cpu_freq    \
({                          \
    u64 __freq;             \
    if(!boot_cpu_has(X86_FEATURE_TSC) || !tsc_khz) \
        __freq = 1000ULL * cpu_khz;           \
    else                            \
        __freq = 1000ULL * tsc_khz;  \
    __freq; \
})

#define __ipipe_hrclock_freq	__ipipe_cpu_freq

#ifdef CONFIG_X86_32

#define ipipe_read_tsc(t)				\
	__asm__ __volatile__("rdtsc" : "=A"(t))

#define ipipe_tsc2ns(t)					\
({							\
	unsigned long long delta = (t) * 1000000ULL;	\
	unsigned long long freq = __ipipe_hrclock_freq;	\
	do_div(freq, 1000);				\
	do_div(delta, (unsigned)freq + 1);		\
	(unsigned long)delta;				\
})

#define ipipe_tsc2us(t)					\
({							\
	unsigned long long delta = (t) * 1000ULL;	\
	unsigned long long freq = __ipipe_hrclock_freq;	\
	do_div(freq, 1000);				\
	do_div(delta, (unsigned)freq + 1);		\
	(unsigned long)delta;				\
})

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
	__asm__("bsrl %1, %0":"=r"(ul) : "r"(ul));
	return ul;
}

#else  /* X86_64 */

#define ipipe_read_tsc(t)  do {		\
	unsigned int __a,__d;			\
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	(t) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

#define ipipe_tsc2ns(t)	(((t) * 1000UL) / (__ipipe_hrclock_freq / 1000000UL))
#define ipipe_tsc2us(t)	((t) / (__ipipe_hrclock_freq / 1000000UL))

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
      __asm__("bsrq %1, %0":"=r"(ul)
	      :	"rm"(ul));
      return ul;
}

#ifdef CONFIG_IA32_EMULATION
#define ipipe_root_nr_syscalls(ti)	\
	((ti->status & TS_COMPAT) ? IA32_NR_syscalls : NR_syscalls)
#endif /* CONFIG_IA32_EMULATION */

#endif	/* X86_64 */

struct pt_regs;
struct irq_desc;
struct ipipe_vm_notifier;

static inline unsigned __ipipe_get_irq_vector(int irq)
{
#ifdef CONFIG_X86_IO_APIC
	unsigned int __ipipe_get_ioapic_irq_vector(int irq);
	return __ipipe_get_ioapic_irq_vector(irq);
#elif defined(CONFIG_X86_LOCAL_APIC)
	return irq >= IPIPE_FIRST_APIC_IRQ ?
		ipipe_apic_irq_vector(irq) : ISA_IRQ_VECTOR(irq);
#else
	return ISA_IRQ_VECTOR(irq);
#endif
}

void ipipe_hrtimer_interrupt(void);

void ipipe_reschedule_interrupt(void);

void ipipe_critical_interrupt(void);

int __ipipe_handle_irq(struct pt_regs *regs);

void __ipipe_handle_vm_preemption(struct ipipe_vm_notifier *nfy);

extern int __ipipe_hrtimer_irq;

#endif	/* !__ASSEMBLY__ */

#endif	/* !__X86_IPIPE_BASE_H */
