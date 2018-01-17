// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/mmu.h>
#include <inc/types.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display information of the kernel stack", mon_backtrace },
	{ "showmappings", "Show physical address mappings corresponding to specific virtual addresses", mon_showmappings }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	cprintf("Stack backtrace:\n");
	uint32_t val_ebp = read_ebp();
	uint32_t* ptr;
	while (val_ebp != 0)
	{
		ptr = (uint32_t*)val_ebp;
		uint32_t eip = *(++ptr);
		cprintf("ebp %x eip %x args ", val_ebp, eip);
		struct Eipdebuginfo info;
		debuginfo_eip((uintptr_t)(*ptr), &info);

		int i;
		for (i = 0; i < 5; i++)
		{
			cprintf("%08x ", *(++ptr));
		}
		cprintf("\n");
		cprintf("%s:%d: %.*s+%u\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
		val_ebp = *((uint32_t*)val_ebp);
	}
	return 0;
}

int char2int(char c)
{
	if (48 <= c && c <= 57)
		return c - 48;
	if ('A' <= c && c <= 'F')
		return c - 55;
	if ('a' <= c && c <= 'f')
		return c - 87;
	return 0;
}

int hexaddr2decaddr(char *hexaddr)
{
	int i;
	int result = 0;
	char *newaddr = hexaddr + 2;
	int tempret = 0;
	int len = strlen(newaddr);
	for (i = 0; i < len; i++, tempret = 0)
	{
		tempret = char2int(newaddr[i]);
		tempret = tempret << ((len - i - 1) * 4);
		result += tempret;	
	}
	return result;
}

int showmappings(uint32_t vaddr)
{
	void* vaddr_ptr = (void*)vaddr;
	pte_t *pte_entry;
	if (page_lookup(kern_pgdir, vaddr_ptr, &pte_entry))
	{
		void *paddr = (void*)PTE_ADDR(*pte_entry);
		cprintf("va: %08p    ", vaddr_ptr);
		cprintf("pa: %08p\n", paddr);
	}
	else
		cprintf("No physical page mapping at %08p\n", vaddr_ptr);
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc == 2)
		showmappings(hexaddr2decaddr(argv[1]));
	else if (argc == 3)
	{
		char *lower_addr = argv[1];
		char *upper_addr = argv[2];
		uint32_t low = hexaddr2decaddr(lower_addr);
		uint32_t high = hexaddr2decaddr(upper_addr);
		while(low <= high)
		{
			showmappings(low);
			low += PGSIZE;
		}
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
