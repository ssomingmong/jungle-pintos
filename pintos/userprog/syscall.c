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

#include "threads/vaddr.h"
#include "threads/mmu.h"

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
void check_string(const char *str) {
	if(str == NULL)
		exit_with_status(-1);
	while(*str != '\0') {
		check_address(str);
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

// 현재 유저 프로세스를 주어진 status 값으로 종료하는 함수
static void
exit_process (int status)
{
	// 현재 실행 중인 스레드의 exit_status에 종료 코드를 저장
	thread_current ()->exit_status = status;
	thread_exit ();
}

// 유저가 넘긴 주소 하나가 커널이 접근해도 되는 유효한 유저 주소인지 검사하는 함수
static void
check_address (const void *uaddr)
{
	// NULL 포인터는 유저 메모리 주소로 사용할 수 없다
	if (uaddr == NULL)
	{
		exit_process (-1);
	}

	// 주소가 커널 영역이면 잘못된 접근
	if (!is_user_vaddr (uaddr))
	{
		exit_process (-1);
	}

	// 유저 주소 범위 안에 있어도 실제 물리 페이지에 매핑되어 있어야 함
	if (pml4_get_page (thread_current ()->pml4, uaddr) == NULL)
	{
		exit_process (-1);
	}
}

// 유저가 남긴 버퍼 전체가 커널이 읽어도 되는 유효한 유저 메모리인지 검사하는 함수
static void
check_buffer (const void *buffer, unsigned size)
{
	// 바이트 단위로 버퍼 주소를 검사하기 위해 uint8_t 포인터로 변환
	const uint8_t *addr = buffer;

	// size가 0이면 검사할 바이트가 없으므로 바로 통과
	if (size == 0)
	{
		return;
	}

	// 버퍼의 첫 바이트부터 마지막 바이트까지 하나씩 검사 
	for (unsigned i = 0; i < size; i++)
	{
		// 현재 바이트 주소가 유효한 유저 주소인지 검사
		check_address (addr + i);
	}
}

// 유저가 넘긴 문자열이 끝까지 유효한 유저 메모리에 있는지 검사하는 함수
static void
check_string (const char *str)
{
	// 문자열의 각 문자를 처음부터 끝의 '\0'까지 검사
	while (true)
	{
		// 현재 문자 주소가 유효한 유저 주소인지 먼저 검사
		check_address (str);

		// 현재 문자가 문자열의 끝이면 검사를 종료
		if (*str == '\0')
		{
			break;
		}
		// 다음 문자 주소로 이동
		str++;
	}
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

		default:
			/* 아직 구현하지 않은 syscall 번호가 들어오면
			 * 우선 현재 프로세스를 종료한다. */
			exit_with_status(-1);
			break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}