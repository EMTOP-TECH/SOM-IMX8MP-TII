/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Declarations of procedures and variables shared between files
 * in arch/ppc/mm/.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */
#include <linux/mm.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_MMU_NOHASH
#include <asm/trace.h>

/*
 * On 8xx, we directly inline tlbia
 */
#ifdef CONFIG_PPC_8xx
static inline void _tlbil_all(void)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
	trace_tlbia(MMU_NO_CONTEXT);
}
static inline void _tlbil_pid(unsigned int pid)
{
	asm volatile ("sync; tlbia; isync" : : : "memory");
	trace_tlbia(pid);
}
#define _tlbil_pid_noind(pid)	_tlbil_pid(pid)

#else /* CONFIG_PPC_8xx */
extern void _tlbil_all(void);
extern void _tlbil_pid(unsigned int pid);
#ifdef CONFIG_PPC_BOOK3E_64
extern void _tlbil_pid_noind(unsigned int pid);
#else
#define _tlbil_pid_noind(pid)	_tlbil_pid(pid)
#endif
#endif /* !CONFIG_PPC_8xx */

/*
 * On 8xx, we directly inline tlbie, on others, it's extern
 */
#ifdef CONFIG_PPC_8xx
static inline void _tlbil_va(unsigned long address, unsigned int pid,
			     unsigned int tsize, unsigned int ind)
{
	asm volatile ("tlbie %0; sync" : : "r" (address) : "memory");
	trace_tlbie(0, 0, address, pid, 0, 0, 0);
}
#elif defined(CONFIG_PPC_BOOK3E_64)
extern void _tlbil_va(unsigned long address, unsigned int pid,
		      unsigned int tsize, unsigned int ind);
#else
extern void __tlbil_va(unsigned long address, unsigned int pid);
static inline void _tlbil_va(unsigned long address, unsigned int pid,
			     unsigned int tsize, unsigned int ind)
{
	__tlbil_va(address, pid);
}
#endif /* CONFIG_PPC_8xx */

#if defined(CONFIG_PPC_BOOK3E_64) || defined(CONFIG_PPC_47x)
extern void _tlbivax_bcast(unsigned long address, unsigned int pid,
			   unsigned int tsize, unsigned int ind);
#else
static inline void _tlbivax_bcast(unsigned long address, unsigned int pid,
				   unsigned int tsize, unsigned int ind)
{
	BUG();
}
#endif

static inline void print_system_hash_info(void) {}

#else /* CONFIG_PPC_MMU_NOHASH */

void print_system_hash_info(void);

#endif /* CONFIG_PPC_MMU_NOHASH */

void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		unsigned long size, unsigned long flags, unsigned int pid);

void cleartlbcam(unsigned long virt, unsigned int pid);

#ifdef CONFIG_PPC32

extern void mapin_ram(void);
extern void setbat(int index, unsigned long virt, phys_addr_t phys,
		   unsigned int size, pgprot_t prot);

extern u8 early_hash[];

#endif /* CONFIG_PPC32 */

extern unsigned long __max_low_memory;
extern phys_addr_t total_memory;
extern phys_addr_t total_lowmem;
extern phys_addr_t memstart_addr;
extern phys_addr_t lowmem_end_addr;

/* ...and now those things that may be slightly different between processor
 * architectures.  -- Dan
 */
#ifdef CONFIG_PPC32
extern void MMU_init_hw(void);
void MMU_init_hw_patch(void);
unsigned long mmu_mapin_ram(unsigned long base, unsigned long top);
#endif
void mmu_init_secondary(int cpu);

#ifdef CONFIG_PPC_E500
extern unsigned long map_mem_in_cams(unsigned long ram, int max_cam_idx,
				     bool dryrun, bool init);
#ifdef CONFIG_PPC32
extern void adjust_total_lowmem(void);
extern int switch_to_as1(void);
extern void restore_to_as0(int esel, int offset, void *dt_ptr, int bootcpu);
void create_kaslr_tlb_entry(int entry, unsigned long virt, phys_addr_t phys);
void reloc_kernel_entry(void *fdt, int addr);
void relocate_init(u64 dt_ptr, phys_addr_t start);
extern int is_second_reloc;
#endif
extern void loadcam_entry(unsigned int index);
extern void loadcam_multi(int first_idx, int num, int tmp_idx);

#ifdef CONFIG_RANDOMIZE_BASE
void kaslr_early_init(void *dt_ptr, phys_addr_t size);
void kaslr_late_init(void);
#else
static inline void kaslr_early_init(void *dt_ptr, phys_addr_t size) {}
static inline void kaslr_late_init(void) {}
#endif

struct tlbcam {
	u32	MAS0;
	u32	MAS1;
	unsigned long	MAS2;
	u32	MAS3;
	u32	MAS7;
};

#define NUM_TLBCAMS	64

extern struct tlbcam TLBCAM[NUM_TLBCAMS];
#endif

#if defined(CONFIG_PPC_BOOK3S_32) || defined(CONFIG_PPC_85xx) || defined(CONFIG_PPC_8xx)
/* 6xx have BATS */
/* PPC_85xx have TLBCAM */
/* 8xx have LTLB */
phys_addr_t v_block_mapped(unsigned long va);
unsigned long p_block_mapped(phys_addr_t pa);
#else
static inline phys_addr_t v_block_mapped(unsigned long va) { return 0; }
static inline unsigned long p_block_mapped(phys_addr_t pa) { return 0; }
#endif

#if defined(CONFIG_PPC_BOOK3S_32) || defined(CONFIG_PPC_8xx) || defined(CONFIG_PPC_E500)
int mmu_mark_initmem_nx(void);
int mmu_mark_rodata_ro(void);
#else
static inline int mmu_mark_initmem_nx(void) { return 0; }
static inline int mmu_mark_rodata_ro(void) { return 0; }
#endif

#ifdef CONFIG_PPC_8xx
void __init mmu_mapin_immr(void);
#endif

static inline bool debug_pagealloc_enabled_or_kfence(void)
{
	return IS_ENABLED(CONFIG_KFENCE) || debug_pagealloc_enabled();
}

#ifdef CONFIG_MEMORY_HOTPLUG
int create_section_mapping(unsigned long start, unsigned long end,
			   int nid, pgprot_t prot);
#endif

int hash__kernel_map_pages(struct page *page, int numpages, int enable);
