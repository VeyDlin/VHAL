#include <cstdlib>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>



/*
 * The default pulls in 70K of garbage
 */
namespace __gnu_cxx {
	void __verbose_terminate_handler(){
		while(true);
	}
}


/*
 * The default pulls in about
 */

__extension__ typedef int __guard __attribute__((mode (__DI__)));


extern "C"  int __cxa_guard_acquire(__guard *g) {
	return !*(char *)(g);
}

extern "C"  void __cxa_guard_release(__guard *g) {
	*(char *)g = 1;
}

extern "C"  void __cxa_guard_abort(__guard *) { }

extern "C"  void __cxa_pure_virtual(void) { }


/*
 * Implement C++ new/delete operators using the heap
 */
void *operator new(size_t size){
	return malloc(size);
}

void *operator new[](size_t size){
	return malloc(size);
}

void operator delete(void *p){
	free(p);
}

void operator delete[](void *p){
	free(p);
}





extern "C" {

	/* Variables */
	extern int errno;
	extern int __io_putchar(char ch) __attribute__((weak));
	extern int __io_getchar(void) __attribute__((weak));
	static uint8_t *__sbrk_heap_end = NULL;

	register char * stack_ptr asm("sp");
	char *__env[1] = {0};
	char **environ = __env;


	/* Functions */
	void* _sbrk(ptrdiff_t incr) {
		extern uint8_t _end; /* Symbol defined in the linker script */
		extern uint8_t _estack; /* Symbol defined in the linker script */
		extern uint32_t _Min_Stack_Size; /* Symbol defined in the linker script */
		const uint32_t stack_limit = (uint32_t)&_estack - (uint32_t)&_Min_Stack_Size;
		const uint8_t *max_heap = (uint8_t *)stack_limit;
		uint8_t *prev_heap_end;

		/* Initialize heap end at first call */
		if (NULL == __sbrk_heap_end)
		{
			__sbrk_heap_end = &_end;
		}

		/* Protect heap from growing into the reserved MSP stack */
		if (__sbrk_heap_end + incr > max_heap)
		{
			errno = ENOMEM;
			return (void *)-1;
		}

		prev_heap_end = __sbrk_heap_end;
		__sbrk_heap_end += incr;

		return (void *)prev_heap_end;
	}


	void initialise_monitor_handles() {
	}

	int _getpid(void){
		return 1;
	}

	int _kill(int pid, int sig){
		errno = EINVAL;
		return -1;
	}

	void _exit(int status){
		_kill(status, -1);
		while(1);
	}

	__attribute__((weak)) int _read(int file, char *ptr, int len){
		int DataIdx;

		for(DataIdx = 0; DataIdx < len; DataIdx++) {
			*ptr++ = __io_getchar();
		}

		return len;
	}

	__attribute__((weak)) int _write(int file, char *ptr, int len){
		int DataIdx;

		for(DataIdx = 0; DataIdx < len; DataIdx++) {
			__io_putchar(*ptr++);
		}
		return len;
	}

	int _close(int file){
		return -1;
	}

	int _fstat(int file, struct stat *st){
		st->st_mode = S_IFCHR;
		return 0;
	}

	int _isatty(int file){
		return 1;
	}

	int _lseek(int file, int ptr, int dir){
		return 0;
	}

	int _open(char *path, int flags, ...){
		/* Pretend like we always fail */
		return -1;
	}

	int _wait(int *status){
		errno = ECHILD;
		return -1;
	}

	int _unlink(char *name){
		errno = ENOENT;
		return -1;
	}

	int _times(struct tms *buf){
		return -1;
	}

	int _stat(char *file, struct stat *st){
		st->st_mode = S_IFCHR;
		return 0;
	}

	int _link(char *old, char *_new) {
		errno = EMLINK;
		return -1;
	}

	int _fork(void){
		errno = EAGAIN;
		return -1;
	}

	int _execve(char *name, char **argv, char **env){
		errno = ENOMEM;
		return -1;
	}
}
