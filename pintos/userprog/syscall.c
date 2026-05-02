#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/init.h"
#include "lib/kernel/stdio.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// 왜 rax를 쓰는가? -> rax에 무슨 syscall인지 넣어둔다.
	switch  (f->R.rax) {
		case SYS_HALT:
			power_off ();
			break;

		case SYS_EXIT:
			thread_exit ();
			break;

		case SYS_WRITE:
			// rdi는 fd 몇번인지 알려주는 레지스터. fd 1은 stdout
			if (f->R.rdi == 1) {
				// rsi는 버퍼 주소, rdx는 바이트 수
				putbuf((char *)f->R.rsi, f->R.rdx);
				// write의 반환값을 size로 설정. 그래야 반환값 역할을 할 수 있음.
				// 정확히는 size 만큼의 바이트를 반환한다는 의미가 된다. (실제로 쓴 바이트 수 반환)
				f->R.rax = f->R.rdx;
			} else {
				f->R.rax = -1;
			}
			break;

		default:
			thread_exit ();
	}
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	// thread_exit ();
}
