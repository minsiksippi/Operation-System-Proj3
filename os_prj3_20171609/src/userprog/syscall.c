#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "pagedir.h"
#include "filesys/off_t.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include <string.h>

static void syscall_handler (struct intr_frame *);
struct lock lockflag;
struct lock mutex;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&lockflag);
}

void check_add(void *address)
{
	struct thread *thrmin = thread_current();
	if (is_user_vaddr(address) && !is_kernel_vaddr(address) && pagedir_get_page(thrmin->pagedir, address))
		;
	else exit(-1);
}

void user_input(int cnt, void* args[], void* esp)
{
	int operation = 5;
	for (int i = 0; i< cnt; i++)
	{
		args[i] = (void*)((unsigned*)esp + i + 1);
		check_add(args[i]);
	}
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *type = (f->esp);
  void *args[4];

  switch(*(int*)type){
  	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		user_input(1, args, f->esp);
		exit((int)*(int*)args[0]);
		break;
	case SYS_EXEC:
		user_input(1, args, f->esp);
		f->eax = exec((char*)*(unsigned*)args[0]);
		break;
	case SYS_WAIT:
		user_input(1, args, f->esp);
		f->eax = wait((int)*(int*)args[0]);
		break;
	case SYS_WRITE:
		user_input(3, args, f->esp);
		f->eax = write((int)*(int*)args[0], (void*)*(int*)args[1], (size_t)*(int*)args[2]);
		break;
	case SYS_READ:
		user_input(3, args, f->esp);
		f->eax = read((int)*(int*)args[0], (void*)*(int*)args[1], (size_t)*(int*)args[2]);
  		break;
	case SYS_FIBO:
		user_input(1, args, f->esp);
		f->eax = fibonacci((int)*(int*)args[0]);
		break;
	case SYS_MAX:
		user_input(4, args, f->esp);
		f->eax = max_of_four_int((int)*(int*)args[0], (int)*(int*)args[1], (int)*(int*)args[2], (int)*(int*)args[3]);
		break;
	case SYS_CREATE:
		user_input(2, args, f->esp);
		f->eax = create((const char*)*(unsigned*)args[0], (unsigned)*(unsigned*)args[1]);
 		break;
	case SYS_REMOVE:
		user_input(1, args, f->esp);
		f->eax = remove((const char*)*(unsigned*)args[0]);
		break;
	case SYS_OPEN:
		user_input(1, args, f->esp);
		f->eax = open((const char*)*(unsigned*)args[0]);
		break;
	case SYS_CLOSE:
		user_input(1, args, f->esp);
		close((int)*(int*)args[0]);
		break;
	case SYS_FILESIZE:
		user_input(1, args, f->esp);
		f->eax = filesize((int)*(int*)args[0]);
		break;
	case SYS_SEEK:
		user_input(2, args, f->esp);
		seek((int)*(int*)args[0], (unsigned)*(unsigned*)args[1]);
		break;
	case SYS_TELL:
		user_input(1, args, f->esp);
		f->eax = tell((int)*(int*)args[0]);
		break;
	default:
		break;
  }

}

void halt()
{
	shutdown_power_off();
}

void exit(int status)
{
	struct thread* cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	
	for(int i=3; i<128; i++)
	{
		if(cur->fd[i] != NULL) close(i);
	}
	thread_exit();
}

pid_t exec(const char* cmd_line)
{
	return process_execute(cmd_line);
}

int wait(pid_t pid)
{
	return process_wait(pid);
}

int write(int fd, const void* buffer, unsigned size)
{
	struct thread *thread_cur = thread_current();
	int file_wir;

	check_add(buffer);
	lock_acquire(&lockflag);

	if(fd == 1)
	{
		putbuf((char*)buffer, size);
		lock_release(&lockflag);
		return size;
	}
	else
	{
		if(thread_cur->fd[fd] == NULL)
		{
			lock_release(&lockflag);
			exit(-1);
		}
		file_wir = file_write(thread_cur->fd[fd], buffer, size);
		lock_release(&lockflag);
		return file_wir;
	}
	lock_release(&lockflag);
	return -1;
}

int read(int fd, void* buffer, unsigned size)
{
	int file_wir;
	struct thread *thread_cur = thread_current();
	
	check_add(buffer);
	lock_acquire(&lockflag);
	
	if(fd == 0)
	{
		for (unsigned int i = 0; i < size; i++)
		{
			((char*)buffer)[i] = (char)input_getc();
		}
		lock_release(&lockflag);
		return size;
	}
	else
	{
		if(thread_cur->fd[fd] == NULL)
		{
			lock_release(&lockflag);
			exit(-1);
		}
		file_wir = file_read(thread_cur->fd[fd], buffer, size);
		lock_release(&lockflag);
		return file_wir;
	}
	
	lock_release(&lockflag);
	return -1;
}

int fibonacci(int n)
{
	int fibo1, fibo2, fibo3;
	fibo1 = 1, fibo2 = 1;

	if (n<0) return 0;
	else if (n<3)
		return 1;
	else {
		for (int i = 3; i <= n; i++) {
			fibo3 = fibo1 + fibo2;
			fibo1 = fibo2;
			fibo2 = fibo3;
		}
		return fibo3;
	}
}

int max_of_four_int(int num1, int num2, int num3, int num4)
{
	int max;
	max = num1;
	if (max< num2) max = num2;
	if (max< num3) max = num3;
	if (max< num4) max = num4;
	return max;
}

bool create(const char *file, unsigned initial_size)
{
	if(file==NULL) exit(-1);
	return filesys_create(file, initial_size);
}

bool remove(const char* file)
{
	if(file==NULL) exit(-1);
	return filesys_remove(file);
}

int open(const char* file)
{
	struct thread *cur = thread_current();
	struct file* filest;
	int state = -1, fd;

	if(file==NULL)
		exit(-1);

	lock_acquire(&lockflag);
	filest = filesys_open(file);

	if (filest)
	{
		for (int i = 3; i < 128; i++)
		{
			if (cur->fd[i] == NULL)
			{
				if (!strcmp(cur->name, file))
					file_deny_write(filest);
				cur->fd[i] = filest;
				state = i;
				break;
			}
		}
	}
	else
		state = -1;

	lock_release(&lockflag);
	return state;
}

void close(int fd) {
	struct file *curfd = thread_current()->fd[fd];

	if (curfd == NULL)
		exit(-1);
	thread_current()->fd[fd] = NULL;
	file_close(curfd);
}

int filesize(int fd)
{
	struct thread *cur = thread_current();
	return file_length(cur->fd[fd]);
}

void seek(int fd, unsigned position)
{
	struct thread *cur = thread_current();
	file_seek(cur->fd[fd], position);
}

unsigned tell(int fd)
{
	struct thread *cur = thread_current();
	return file_tell(cur->fd[fd]);
}
