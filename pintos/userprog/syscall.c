#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
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
	/* f->R.rax 에는 유저 프로그램이 요청한 syscall 번호가 들어 있다.
	 * 이 번호를 기준으로 어떤 작업을 할지 분기한다. */
	switch(f->R.rax){
		case SYS_HALT:
			/* halt()는 현재 프로세스만 끝내는 것이 아니라
			 * Pintos 자체를 종료하는 시스템콜이다. */
			power_off();
			break;
		
		case SYS_EXIT:
			/* exit()는 현재 유저 프로그램을 종료한다.
			 * 지금 첫 단계에서는 간단히 thread_exit()만 호출한다. */
			thread_exit();
			break;

		case SYS_WRITE:
			/* write(fd, buffer, size)의 세 인자는
			 * 첫 번째부터 차례로 rdi, rsi, rdx에 들어온다. */
			int fd = f->R.rdi;
			void *buffer = f->R.rsi;
			int size = f->R.rdx;

			if(fd == 1) {
				/* fd == 1은 stdout이므로
				 * 화면(콘솔)에 문자열을 출력한다. */
				putbuf(buffer, size);

				/* syscall 반환값은 rax로 돌려준다.
				 * write 성공 시에는 출력한 바이트 수를 반환한다. */
				f->R.rax = size;
			}
			else {
				/* 지금은 stdout만 지원하므로
				 * 다른 fd는 실패로 처리한다. */
				f->R.rax = -1;
			}
			break;

		default:
			/* 아직 구현하지 않은 syscall 번호가 들어오면
			 * 우선 현재 프로세스를 종료한다. */
			thread_exit();
			break;
	}
	printf ("system call!\n");
	thread_exit ();
}
