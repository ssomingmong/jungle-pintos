#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "lib/kernel/stdio.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* exit_status 저장 후 스레드 종료 */
void exit_with_status(int status){
	thread_current()->exit_status = status;
	thread_exit();
}

/* 주소 하나가 안전한지 검사 — NULL, 커널 영역, 미매핑 주소면 종료 */
void check_address(const void *addr) {
	if(addr == NULL || is_user_vaddr(addr) == 0 || pml4_get_page(thread_current()->pml4, addr) == NULL) {
		exit_with_status(-1);
	}
}

/* 버퍼 전체 범위(시작~끝)가 안전한지 검사 */
void check_buffer(void *buffer, int size) {
	(char*)buffer;
	char* bf_end = buffer + size - 1;
	
	for(;buffer <= bf_end; buffer++) {
		check_address(buffer);		
	}
}

/* 문자열이 '\0'까지 전부 안전한지 한 글자씩 검사 */
// 유저 포인터는 역참조하기 전에 먼저 검사한다.
void check_string(const char *str) {
	if(str == NULL)
		exit_with_status(-1);

	while (true) {
		/* 현재 문자를 읽기 전에 먼저 주소가 안전한지 검사 */
		check_address (str);

		/* 현재 문자가 문자열 끝이면 검사 종료 */
		if (*str == '\0')
			break;

		/* 다음 문자로 이동 */
		str++;
	}
}
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
			/* exit()는 현재 유저 프로그램을 종료한다. */
			exit_with_status(f->R.rdi);
			break;

		case SYS_CREATE:
			const char *file_name;
			unsigned size;

			file_name = f->R.rdi;
			size = f->R.rsi;

			check_address(file_name);

			f->R.rax = filesys_create(file_name, size);
			break;

		case SYS_WRITE: {
			/* write(fd, buffer, size)의 세 인자는
			 * 첫 번째부터 차례로 rdi, rsi, rdx에 들어온다. */
			int fd = (int) f->R.rdi;
			const void *buffer = (const void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;

			// 유저가 넘긴 buffer부터 size 바이트까지 모두 안전한지 검사
			check_buffer (buffer, size);

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
		}

		case SYS_CREATE: {
			/* create의 첫 번째 인자 file은 rdi에 들어옴 */
			const char *file = (const char *) f->R.rdi;

			/* create의 두 번째 인자 initial_size는 rsi에 들어옴 */
			unsigned initial_size = (unsigned) f->R.rsi;

			/* 유저가 넘긴 파일 이름 문자열이 안전한지 먼저 검사 */
			check_string (file);

			/* 실제 파일 시스템에 파일 생성을 요청 */
			bool success = filesys_create (file, initial_size);

			/* create()의 반환값은 성공 true, 실패 false이므로 rax에 저장 */
			f->R.rax = success;

			break;
		}

		default:
			/* 아직 구현하지 않은 syscall 번호가 들어오면
			 * 우선 현재 프로세스를 종료한다. */
			exit_with_status(-1);
			break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}