#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#define EMPTY_MODEL_QUEUE 458

int main()
{
    syscall(EMPTY_MODEL_QUEUE);

    return 0;
}