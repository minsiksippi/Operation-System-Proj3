#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

typedef int pid_t;

void syscall_init (void);

void halt(void);
void exit(int status);
pid_t exec(const char *cmd_line);
int wait(pid_t pid);
int write(int fd, const void *buffer, unsigned size);
int read(int fd, void *buffer, unsigned size);

int fibonacci(int n);
int max_of_four_int(int a, int b, int c, int d);

void check_add(void *address);
void user_input(int count, void *args[], void* esp);

bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

#endif /* userprog/syscall.h */
