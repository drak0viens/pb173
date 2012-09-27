#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

int main(void)
{
        char * str = "Hi\n";
	printf("syscall retval=%ld\n", syscall(__NR_write, 1, (void *) str, (size_t) 3));
	return 0;
}
