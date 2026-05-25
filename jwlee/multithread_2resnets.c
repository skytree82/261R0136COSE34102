#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <spawn.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <stdlib.h>

#define NV_SCHEDULER_INIT 455

/*
    멀티스레드에서 각각의 스레드가 별도의 자식 프로세스를 생성하여 서로 다른 작업을 동시에 수행하도록 구현.
    main 스레드는 두 resnet 을 정의하고, 각각의 작업에 대해 별도의 스레드를 생성하여 handling.
    각 스레드는 posix_spawnp를 사용하여 자식 '프로세스'를 생성
    자식 프로세스가 종료될 때까지 대기 후 종료 상태를 확인하고 로그를 출력.
*/

/*
    실행 파일 위치
    resnet50-1-linux, resnet50-2-linux: 현재 디렉토리 (./)
*/


// 현재 프로세스의 환경 변수 포인터 배열. posix_spawnp에 넘겨서 자식 프로세스가 같은 환경변수 사용하도록.
extern char **environ;


// 실행할 작업 구조체
typedef struct
{
    const char *name;   // 작업 이름
    char *const *argv;  // 실행할 명령어와 인자들
} task_t;


// 각 Thread 핸들링 함수
static void *run_task(void *arg) 
{
    // pthread 함수의 인자는 void 포인터이므로 적절히 변환 필요.
    // void 포인터를 task_t 포인터로 변환.
    const task_t *task = (const task_t *)arg;
    pid_t pid;

    // posix_spawnp(&pid, file, file_actions, attrp, argv, envp)
    int rc = posix_spawnp(&pid, task->argv[0], NULL, NULL, task->argv, environ);

    // 자식 프로세스 생성 실패 시 에러 처리
    if (rc != 0)
    {
        fprintf(stderr, "[ERROR] %s: failed to spawn '%s': %s\n", task->name, task->argv[0], strerror(rc));
        return (void *)1;
    }

    // 자식 프로세스가 종료될 때까지 대기

    // waitpid 후 종료 상태 저장 변수
    int status = 0;

    // waitpid(pid_t pid, int *status, int options)
    // option = 0 으로 해당 자식 프로세스가 종료될 때까지 블로킹
    if (waitpid(pid, &status, 0) < 0)
    {
        fprintf(stderr, "[ERROR] %s: waitpid failed: %s\n", task->name, strerror(errno));
        return (void *)1;
    }

    // 자식 프로세스의 종료 상태 확인 및 로그 출력
    if (WIFEXITED(status))
        fprintf(stderr, "[INFO] %s: exited with code %d\n", task->name, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "[INFO] %s: killed by signal %d\n", task->name, WTERMSIG(status)); 
    else
        // 정상 종료
        fprintf(stderr, "[INFO] %s: finished (status=0x%x)\n", task->name, status);

    return NULL;
}


int main(int argc, char **argv)
{
    int slack_weight = 1;
    int aging_weight = 8;
    int streak_penalty_step = 12;

    if (argc == 4) {
        slack_weight = atoi(argv[1]);
        aging_weight = atoi(argv[2]);
        streak_penalty_step = atoi(argv[3]);
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [slack_weight aging_weight streak_penalty_step]\n", argv[0]);
        return 1;
    }

    if (syscall(NV_SCHEDULER_INIT, slack_weight, aging_weight, streak_penalty_step) < 0) {
        fprintf(stderr, "[ERROR] nv_scheduler_init failed: %s\n", strerror(errno));
        return 1;
    }

    fprintf(stderr, "[INFO] scheduler weights: slack=%d aging=%d streak=%d\n",
        slack_weight, aging_weight, streak_penalty_step);
    
    // [ResNet1]
    char *resnet_argv[] =
    {
        "./resnet50-1-linux",
        /* 
            필요 시 인자 추가 
            1번째 인자: cpu | os | ws (없으면 ws)
            2번째 인자: conv | matmul (없으면 conv)
            3번째 인자: check (없으면 false, debuging용 결과 맞는지 확인)
        */
        NULL
    };

    // [ResNet2]
    char *resnet_argv2[] =
    {
        "./resnet50-2-linux",
        /* 
            필요 시 인자 추가 
            1번째 인자: cpu | os | ws (없으면 ws)
            2번째 인자: conv | matmul (없으면 conv)
            3번째 인자: check (없으면 false, debuging용 결과 맞는지 확인)
        */
        NULL
    };

    task_t tasks[2] =
    {
        {"ResNet1", resnet_argv},
        {"ResNet2", resnet_argv2},
    };

    pthread_t threads[2];
    for (int i = 0; i < 2; i++)
    {
        int rc = pthread_create(&threads[i], NULL, run_task, &tasks[i]);

        if (rc != 0)
        {
            fprintf(stderr, "[ERROR] pthread_create(%s) failed: %s\n", tasks[i].name, strerror(rc));

            // 이미 생성된 스레드들은 join해서 종료될 때까지 main thread 대기.
            for (int j = 0; j < i; j++)
                (void)pthread_join(threads[j], NULL);
            return 1;
        }
    }

    // 종료 상태 저장 변수
    int exit_code = 0;
    for (int i = 0; i < 2; i++)
    {
        void *ret = NULL;
        if (pthread_join(threads[i], &ret) != 0)
            exit_code = 1;
        if (ret != NULL)
            exit_code = 1;
    }

    return exit_code;
}