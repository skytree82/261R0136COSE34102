#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Infinite multimodel deadline evaluator.
 *
 * Usage:
 *   ./multimodel_infinite.riscv -s 1 15 -d 580 250 1000 -p 580 250 1000
 *
 * Deadlines and release periods are in ms. If -p is omitted, each model's
 * release period defaults to its deadline.
 * Every release creates a new job thread, so overlapped jobs are possible.
 */

extern char **environ;

#define NV_SCHEDULER_INIT 455

#define DEFAULT_SLACK_WEIGHT         1
#define DEFAULT_STREAK_LIMIT         15
#define STOP_POLL_INTERVAL_MS       100

#define DEFAULT_RESNET_DEADLINE_MS     2055
#define DEFAULT_MOBILENET_DEADLINE_MS  885
#define DEFAULT_BITNET_DEADLINE_MS    81350

#define RESNET_PATH    "./resnet50_irq_rvv_no_op_wait-linux"
#define MOBILENET_PATH "./mobilenet_irq_rvv_no_op_wait-linux"

#define BITNET_SERVER_PATH  "./llama-server"
#define BITNET_MODEL_PATH   "models/i2s_NPU_16.gguf"
#define BITNET_HOST         "127.0.0.1"
#define BITNET_PORT         "8080"
#define BITNET_HEALTH_URL   "http://127.0.0.1:8080/health"
#define BITNET_COMPLETION_URL "http://127.0.0.1:8080/completion"
#define BITNET_SERVER_LOG   "llama-server.log"

#ifndef PRINT_ENABLE
#define PRINT_ENABLE 0
#endif

#define EVAL_PRINT(...) do { if (PRINT_ENABLE) fprintf(stderr, __VA_ARGS__); } while (0)

#ifndef UTIL_ENABLE
#define UTIL_ENABLE 1
#endif

#if UTIL_ENABLE
static inline uint64_t read_cycle_csr(void) {
    uint64_t value;
    asm volatile ("csrr %0, cycle" : "=r"(value));
    return value;
}

static inline uint64_t read_hpmcounter3(void) {
    uint64_t value;
    asm volatile ("csrr %0, hpmcounter3" : "=r"(value));
    return value;
}

static inline uint64_t read_hpmcounter4(void) {
    uint64_t value;
    asm volatile ("csrr %0, hpmcounter4" : "=r"(value));
    return value;
}

static uint64_t util_window_start_cycle = 0;
static uint64_t util_window_end_cycle = 0;
static uint64_t util_window_start_rocc = 0;
static uint64_t util_window_end_rocc = 0;
static uint64_t util_window_start_vec = 0;
static uint64_t util_window_end_vec = 0;
static int util_window_started = 0;
static int util_window_finished = 0;

#endif

typedef enum {
    MODEL_RESNET = 0,
    MODEL_MOBILENET,
    MODEL_BITNET,
    MODEL_COUNT
} model_id_t;

typedef struct {
    const char *name;
    unsigned int deadline_ms;
    unsigned int period_ms;
    unsigned int releases;
    unsigned int completed;
    unsigned int success;
    unsigned int missed;
    unsigned int failed;
    unsigned int active;
    double total_ms;
    double min_ms;
    double max_ms;
} model_stat_t;

typedef struct {
    model_id_t model_id;
    unsigned long job_id;
#if UTIL_ENABLE
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t start_rocc;
    uint64_t end_rocc;
    uint64_t start_vec;
    uint64_t end_vec;
#endif
} job_arg_t;

typedef struct {
    model_id_t model_id;
} releaser_arg_t;

static volatile sig_atomic_t stop_requested = 0;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stats_cond = PTHREAD_COND_INITIALIZER;
static model_stat_t stats[MODEL_COUNT] = {
    [MODEL_RESNET] = {
        .name = "ResNet50",
        .deadline_ms = DEFAULT_RESNET_DEADLINE_MS,
        .period_ms = DEFAULT_RESNET_DEADLINE_MS,
    },
    [MODEL_MOBILENET] = {
        .name = "MobileNet",
        .deadline_ms = DEFAULT_MOBILENET_DEADLINE_MS,
        .period_ms = DEFAULT_MOBILENET_DEADLINE_MS,
    },
    [MODEL_BITNET] = {
        .name = "BitNet",
        .deadline_ms = DEFAULT_BITNET_DEADLINE_MS,
        .period_ms = DEFAULT_BITNET_DEADLINE_MS,
    },
};
static pid_t bitnet_server_pid = -1;
static unsigned int max_bitnet_jobs = 0;   /* 0 means infinite */


static void handle_signal(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-s slack_weight max_consecutive_grants] "
            "[-d resnet_deadline_ms mobilenet_deadline_ms bitnet_deadline_ms] "
            "[-p resnet_period_ms mobilenet_period_ms bitnet_period_ms] "
            "[-n max_bitnet_jobs]\n",
            prog);
    fprintf(stderr,
            "example: %s -s 1 15 -d 580 250 25000 -p 580 250 1000 -n 20\n",
            prog);
    fprintf(stderr,
            "note: legacy '-s slack aging streak' is accepted; aging is ignored.\n");
    fprintf(stderr,
            "note: if -n is set, the program stops after BitNet completes that many jobs.\n");
}

static int parse_positive_uint(const char *value, const char *field, unsigned int *out) {
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        fprintf(stderr, "[ERROR] invalid %s: '%s'\n", field, value);
        return -1;
    }

    *out = (unsigned int)parsed;
    return 0;
}

static int parse_int_arg(const char *value, const char *field, int *out) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < INT32_MIN || parsed > INT32_MAX) {
        fprintf(stderr, "[ERROR] invalid %s: '%s'\n", field, value);
        return -1;
    }

    *out = (int)parsed;
    return 0;
}

static int check_executable(const char *path) {
    if (access(path, X_OK) != 0) {
        fprintf(stderr, "[ERROR] required executable '%s' is not runnable: %s\n",
                path, strerror(errno));
        return -1;
    }

    return 0;
}

static double elapsed_ms(struct timespec start, struct timespec end) {
    int64_t sec = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nsec = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (double)sec * 1000.0 + (double)nsec / 1000000.0;
}

static double timespec_to_ms(struct timespec t) {
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}

static struct timespec timespec_add_ms(struct timespec base, unsigned int ms) {
    base.tv_sec += (time_t)(ms / 1000);
    base.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (base.tv_nsec >= 1000000000L) {
        base.tv_sec += 1;
        base.tv_nsec -= 1000000000L;
    }
    return base;
}

static int timespec_cmp(struct timespec a, struct timespec b) {
    if (a.tv_sec != b.tv_sec) {
        return a.tv_sec < b.tv_sec ? -1 : 1;
    }
    if (a.tv_nsec != b.tv_nsec) {
        return a.tv_nsec < b.tv_nsec ? -1 : 1;
    }
    return 0;
}

static int sleep_until(struct timespec when) {
    while (!stop_requested) {
        struct timespec now;
        struct timespec wake_time;
        int rc;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            fprintf(stderr, "[ERROR] clock_gettime failed: %s\n", strerror(errno));
            return -1;
        }
        if (timespec_cmp(now, when) >= 0) {
            return 0;
        }

        wake_time = timespec_add_ms(now, STOP_POLL_INTERVAL_MS);
        if (timespec_cmp(wake_time, when) > 0) {
            wake_time = when;
        }

        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake_time, NULL);
        if (rc == 0) {
            if (timespec_cmp(wake_time, when) == 0) {
                return 0;
            }
            continue;
        }
        if (rc != EINTR) {
            fprintf(stderr, "[ERROR] clock_nanosleep failed: %s\n", strerror(rc));
            return -1;
        }
    }
    return -1;
}

static int spawn_wait_redirect(char *const argv[], const char *name, int quiet) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actions_ptr = NULL;
    pid_t pid;
    int rc;
    int status = 0;

    if (quiet) {
        rc = posix_spawn_file_actions_init(&actions);
        if (rc != 0) {
            fprintf(stderr, "[ERROR] %s: file_actions_init failed: %s\n", name, strerror(rc));
            return -1;
        }
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        actions_ptr = &actions;
    }

    rc = posix_spawnp(&pid, argv[0], actions_ptr, NULL, argv, environ);
    if (quiet) {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (rc != 0) {
        fprintf(stderr, "[ERROR] %s: failed to spawn '%s': %s\n", name, argv[0], strerror(rc));
        return -1;
    }

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[ERROR] %s: waitpid failed: %s\n", name, strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0 ? 0 : -1;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "[ERROR] %s: killed by signal %d\n", name, WTERMSIG(status));
        return -1;
    }

    fprintf(stderr, "[ERROR] %s: unexpected status 0x%x\n", name, status);
    return -1;
}

static int spawn_bitnet_server(void) {
    posix_spawn_file_actions_t actions;
    char deadline_buf[32];
    snprintf(deadline_buf, sizeof(deadline_buf), "%u", stats[MODEL_BITNET].deadline_ms);

    char *server_argv[] = {
        BITNET_SERVER_PATH,
        "-m", BITNET_MODEL_PATH,
        "--host", BITNET_HOST,
        "--port", BITNET_PORT,
        "--deadline-ms", deadline_buf,
        "-b", "16",
        "-ub", "16",
        "-t", "1",
        "--threads-http", "1",
        "--slot-prompt-similarity", "0",
        "--log-disable",
        NULL
    };
    int rc;

    rc = posix_spawn_file_actions_init(&actions);
    if (rc != 0) {
        fprintf(stderr, "[ERROR] llama-server: file_actions_init failed: %s\n", strerror(rc));
        return -1;
    }
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                     BITNET_SERVER_LOG,
                                     O_WRONLY | O_CREAT | O_TRUNC,
                                     0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

    rc = posix_spawnp(&bitnet_server_pid, server_argv[0], &actions, NULL, server_argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        fprintf(stderr, "[ERROR] failed to spawn llama-server '%s': %s\n",
                server_argv[0], strerror(rc));
        bitnet_server_pid = -1;
        return -1;
    }

    fprintf(stderr, "[INFO] llama-server pid=%d, log=%s\n",
            (int)bitnet_server_pid, BITNET_SERVER_LOG);
    return 0;
}

static int check_bitnet_health(void) {
    FILE *fp;
    char output[128];
    size_t len = 0;
    int status;

    fp = popen("wget -qO- " BITNET_HEALTH_URL " 2>/dev/null", "r");
    if (fp == NULL) {
        fprintf(stderr, "[ERROR] bitnet-health: popen failed: %s\n", strerror(errno));
        return -1;
    }

    while (len + 1 < sizeof(output)) {
        size_t n = fread(output + len, 1, sizeof(output) - len - 1, fp);
        len += n;
        if (n == 0) {
            break;
        }
    }
    output[len] = '\0';

    status = pclose(fp);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }

    return strstr(output, "{\"status\":\"ok\"}") != NULL ? 0 : -1;
}

static int wait_for_bitnet_ready(unsigned int timeout_seconds) {
    for (unsigned int i = 0; !stop_requested && i < timeout_seconds; i++) {
        if (check_bitnet_health() == 0) {
            fprintf(stderr, "[INFO] bitnet server is ready\n");
            return 0;
        }
        sleep(1);
    }

    fprintf(stderr, "[ERROR] bitnet server did not become ready within %u seconds\n",
            timeout_seconds);
    return -1;
}

static int run_resnet_job(unsigned int deadline_ms) {
    char deadline_buf[32];
    snprintf(deadline_buf, sizeof(deadline_buf), "%u", deadline_ms);

    char *argv[] = {
        RESNET_PATH,
        deadline_buf,
        NULL
    };
    return spawn_wait_redirect(argv, "ResNet50", 1);
}

static int run_mobilenet_job(unsigned int deadline_ms) {
    char deadline_buf[32];
    snprintf(deadline_buf, sizeof(deadline_buf), "%u", deadline_ms);

    char *argv[] = {
        MOBILENET_PATH,
        deadline_buf,
        NULL
    };
    return spawn_wait_redirect(argv, "MobileNet", 1);
}

static int run_bitnet_job(unsigned int deadline_ms) {
    char post_data[512];

    snprintf(post_data, sizeof(post_data),
             "--post-data={\"prompt\":\"Please answer the question in one short word only and avoid any extra explanation now\","
             "\"n_predict\":1,\"stream\":false,\"cache_prompt\":false,\"deadline_ms\":%u}",
             deadline_ms);

    char *argv[] = {
        "wget",
        "-qO-",
        "--header=Content-Type: application/json",
        post_data,
        BITNET_COMPLETION_URL,
        NULL
    };

    return spawn_wait_redirect(argv, "BitNet", 1);
}

static void record_result(const job_arg_t *job,
                          struct timespec start, struct timespec end, int ok) {
    model_id_t model_id = job->model_id;
    unsigned long job_id = job->job_id;
    model_stat_t *st = &stats[model_id];
    double runtime_ms = elapsed_ms(start, end);
    int hit = ok && runtime_ms <= (double)st->deadline_ms;
#if UTIL_ENABLE
    uint64_t job_cycles = job->end_cycle - job->start_cycle;
    uint64_t job_rocc_busy = job->end_rocc - job->start_rocc;
    uint64_t job_vec_busy = job->end_vec - job->start_vec;
    unsigned long long job_rocc_pct =
        job_cycles ? (unsigned long long)((100 * job_rocc_busy) / job_cycles) : 0ULL;
    unsigned long long job_vec_pct =
        job_cycles ? (unsigned long long)((100 * job_vec_busy) / job_cycles) : 0ULL;
#endif

    pthread_mutex_lock(&stats_lock);
    st->completed++;
    if (ok) {
        if (hit) {
            st->success++;
        } else {
            st->missed++;
        }
    } else {
        st->failed++;
    }
    st->total_ms += runtime_ms;
    if (st->completed == 1 || runtime_ms < st->min_ms) {
        st->min_ms = runtime_ms;
    }
    if (st->completed == 1 || runtime_ms > st->max_ms) {
        st->max_ms = runtime_ms;
    }
    if (st->active > 0) {
        st->active--;
    }
#if UTIL_ENABLE
    {
        unsigned int total_active_after = 0;
        for (int i = 0; i < MODEL_COUNT; i++) {
            total_active_after += stats[i].active;
        }
        if (util_window_started && !util_window_finished && total_active_after == 0) {
            util_window_end_cycle = read_cycle_csr();
            util_window_end_rocc = read_hpmcounter3();
            util_window_end_vec = read_hpmcounter4();
            util_window_finished = 1;
        }
    }
#endif
    if (model_id == MODEL_BITNET &&
        max_bitnet_jobs > 0 &&
        st->completed >= max_bitnet_jobs) {
        if (!stop_requested) {
            EVAL_PRINT(
                    "[INFO] BitNet completed target reached (%u); stopping workload release\n",
                    max_bitnet_jobs);
        }
        stop_requested = 1;
    }
    pthread_cond_broadcast(&stats_cond);

//     fprintf(stderr, "[RESULT] %s job=%lu start=%.3f ms end=%.3f ms runtime=%.3f ms deadline=%u ms status=%s active=%u"
// #if UTIL_ENABLE
//                " job_cycles=%llu rocc_busy=%llu(%llu%%) vec_busy=%llu(%llu%%)"
// #endif
//                "\n",
//                st->name,
//                job_id,
//                timespec_to_ms(start),
//                timespec_to_ms(end),
//                runtime_ms,
//                st->deadline_ms,
//                ok ? (hit ? "HIT" : "MISS") : "FAIL",
//                st->active
// #if UTIL_ENABLE
//                , (unsigned long long)job_cycles,
//                (unsigned long long)job_rocc_busy,
//                job_rocc_pct,
//                (unsigned long long)job_vec_busy,
//                job_vec_pct
// #endif
//                );
    pthread_mutex_unlock(&stats_lock);
}

static void *run_job(void *arg) {
    job_arg_t *job = (job_arg_t *)arg;
    struct timespec start;
    struct timespec end;
    unsigned int deadline_ms;
    int rc;

    pthread_mutex_lock(&stats_lock);
    deadline_ms = stats[job->model_id].deadline_ms;
    pthread_mutex_unlock(&stats_lock);

    clock_gettime(CLOCK_MONOTONIC, &start);
#if UTIL_ENABLE
    job->start_cycle = read_cycle_csr();
    job->start_rocc = read_hpmcounter3();
    job->start_vec = read_hpmcounter4();
#endif
    switch (job->model_id) {
    case MODEL_RESNET:
        rc = run_resnet_job(deadline_ms);
        break;
    case MODEL_MOBILENET:
        rc = run_mobilenet_job(deadline_ms);
        break;
    case MODEL_BITNET:
        rc = run_bitnet_job(deadline_ms);
        break;
    default:
        rc = -1;
        break;
    }
#if UTIL_ENABLE
    job->end_cycle = read_cycle_csr();
    job->end_rocc = read_hpmcounter3();
    job->end_vec = read_hpmcounter4();
#endif
    clock_gettime(CLOCK_MONOTONIC, &end);

    record_result(job, start, end, rc == 0);
    free(job);
    return NULL;
}

static int release_job(model_id_t model_id, unsigned long *released_job_id) {
    pthread_t thread;
    job_arg_t *job;
    unsigned long job_id;
#if UTIL_ENABLE
    unsigned int total_active_before = 0;
#endif

    job = (job_arg_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        fprintf(stderr, "[ERROR] calloc job failed\n");
        return -1;
    }

    pthread_mutex_lock(&stats_lock);
#if UTIL_ENABLE
    for (int i = 0; i < MODEL_COUNT; i++) {
        total_active_before += stats[i].active;
    }
#endif
    stats[model_id].releases++;
    stats[model_id].active++;
    job_id = stats[model_id].releases;
#if UTIL_ENABLE
    if (!util_window_started && total_active_before == 0) {
        util_window_start_cycle = read_cycle_csr();
        util_window_start_rocc = read_hpmcounter3();
        util_window_start_vec = read_hpmcounter4();
        util_window_started = 1;
        util_window_finished = 0;
        util_window_end_cycle = 0;
        util_window_end_rocc = 0;
        util_window_end_vec = 0;
    }
#endif
    pthread_mutex_unlock(&stats_lock);

    if (released_job_id != NULL) {
        *released_job_id = job_id;
    }

    job->model_id = model_id;
    job->job_id = job_id;

    int rc = pthread_create(&thread, NULL, run_job, job);
    if (rc != 0) {
        fprintf(stderr, "[ERROR] pthread_create(%s job %lu) failed: %s\n",
                stats[model_id].name, job_id, strerror(rc));
        pthread_mutex_lock(&stats_lock);
        stats[model_id].active--;
        stats[model_id].failed++;
        pthread_cond_broadcast(&stats_cond);
        pthread_mutex_unlock(&stats_lock);
        free(job);
        return -1;
    }

    pthread_detach(thread);
    return 0;
}

static void *release_loop(void *arg) {
    releaser_arg_t *ctx = (releaser_arg_t *)arg;
    model_id_t model_id = ctx->model_id;
    struct timespec next_release;
    unsigned int deadline_ms;
    unsigned int period_ms;

    if (clock_gettime(CLOCK_MONOTONIC, &next_release) != 0) {
        fprintf(stderr, "[ERROR] %s: clock_gettime failed: %s\n",
                stats[model_id].name, strerror(errno));
        return (void *)1;
    }

    pthread_mutex_lock(&stats_lock);
    deadline_ms = stats[model_id].deadline_ms;
    period_ms = stats[model_id].period_ms;
    pthread_mutex_unlock(&stats_lock);

    EVAL_PRINT( "[INFO] %s release period=%u ms deadline=%u ms\n",
            stats[model_id].name, period_ms, deadline_ms);

    while (!stop_requested) {
        struct timespec wait_start;
        struct timespec release_time;
        unsigned long job_id = 0;
        double waited_ms;
        double lateness_ms;

        if (clock_gettime(CLOCK_MONOTONIC, &wait_start) != 0) {
            fprintf(stderr, "[ERROR] %s: clock_gettime failed: %s\n",
                    stats[model_id].name, strerror(errno));
            return (void *)1;
        }

        if (sleep_until(next_release) != 0) {
            break;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &release_time) != 0) {
            fprintf(stderr, "[ERROR] %s: clock_gettime failed: %s\n",
                    stats[model_id].name, strerror(errno));
            return (void *)1;
        }

        if (model_id == MODEL_BITNET && max_bitnet_jobs > 0) {
            unsigned int released_so_far;

            pthread_mutex_lock(&stats_lock);
            released_so_far = stats[MODEL_BITNET].releases;
            pthread_mutex_unlock(&stats_lock);

            if (released_so_far >= max_bitnet_jobs) {
                EVAL_PRINT(
                        "[INFO] BitNet release limit reached (%u); no more BitNet jobs will be released\n",
                        max_bitnet_jobs);
                break;
            }
        }

        if (release_job(model_id, &job_id) != 0) {
            stop_requested = 1;
            return (void *)1;
        }

        waited_ms = elapsed_ms(wait_start, release_time);
        lateness_ms = elapsed_ms(next_release, release_time);
        EVAL_PRINT("[RELEASE] %s job=%lu waited=%.3f ms period=%u ms scheduled=%.3f ms actual=%.3f ms lateness=%.3f ms\n",
                   stats[model_id].name,
                   job_id,
                   waited_ms,
                   period_ms,
                   timespec_to_ms(next_release),
                   timespec_to_ms(release_time),
                   lateness_ms);

        next_release = timespec_add_ms(next_release, period_ms);
    }

    return NULL;
}

static void wait_for_active_jobs(void) {
    int active;

    pthread_mutex_lock(&stats_lock);
    do {
        active = 0;
        for (int i = 0; i < MODEL_COUNT; i++) {
            active += (int)stats[i].active;
        }
        if (active > 0) {
            EVAL_PRINT( "[INFO] waiting for %d active jobs to finish\n", active);
            pthread_cond_wait(&stats_cond, &stats_lock);
        }
    } while (active > 0);
    pthread_mutex_unlock(&stats_lock);
}

static void print_stats(void) {
    pthread_mutex_lock(&stats_lock);
    fprintf(stderr, "\n[SUMMARY]\n");
    for (int i = 0; i < MODEL_COUNT; i++) {
        model_stat_t *st = &stats[i];
        double avg = st->completed ? st->total_ms / (double)st->completed : 0.0;
        fprintf(stderr,
                "[SUMMARY] %s: releases=%u completed=%u hit=%u miss=%u fail=%u avg=%.3f ms min=%.3f ms max=%.3f ms deadline=%u ms period=%u ms\n",
                st->name,
                st->releases,
                st->completed,
                st->success,
                st->missed,
                st->failed,
                avg,
                st->completed ? st->min_ms : 0.0,
                st->completed ? st->max_ms : 0.0,
                st->deadline_ms,
                st->period_ms);
    }
#if UTIL_ENABLE
    if (util_window_started) {
        uint64_t end_cycle = util_window_finished ? util_window_end_cycle : read_cycle_csr();
        uint64_t end_rocc = util_window_finished ? util_window_end_rocc : read_hpmcounter3();
        uint64_t end_vec = util_window_finished ? util_window_end_vec : read_hpmcounter4();
        uint64_t total_cycles = end_cycle - util_window_start_cycle;
        uint64_t rocc_busy_cycles = end_rocc - util_window_start_rocc;
        uint64_t vec_busy_cycles = end_vec - util_window_start_vec;

        fprintf(stderr, "[SUMMARY] util_window_cycles=%llu\n",
                (unsigned long long)total_cycles);
        fprintf(stderr, "[SUMMARY] rocc_busy_cycles=%llu (%llu%%)\n",
                (unsigned long long)rocc_busy_cycles,
                total_cycles ? (unsigned long long)((100 * rocc_busy_cycles) / total_cycles) : 0ULL);
        fprintf(stderr, "[SUMMARY] vector_backend_busy_cycles=%llu (%llu%%)\n",
                (unsigned long long)vec_busy_cycles,
                total_cycles ? (unsigned long long)((100 * vec_busy_cycles) / total_cycles) : 0ULL);
    }
#endif
    pthread_mutex_unlock(&stats_lock);
}

static void stop_bitnet_server(void) {
    if (bitnet_server_pid <= 0) {
        return;
    }

    EVAL_PRINT( "[INFO] stopping llama-server pid=%d\n", (int)bitnet_server_pid);
    kill(bitnet_server_pid, SIGTERM);
    waitpid(bitnet_server_pid, NULL, 0);
    bitnet_server_pid = -1;
}

int main(int argc, char **argv) {
    int slack_weight = DEFAULT_SLACK_WEIGHT;
    int streak_limit = DEFAULT_STREAK_LIMIT;
    pthread_t releasers[MODEL_COUNT];
    releaser_arg_t releaser_args[MODEL_COUNT];
    struct sigaction sa = {0};
    int period_was_set = 0;
    int exit_code = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            int ignored_aging_weight;

            if (i + 2 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            if (parse_int_arg(argv[i + 1], "slack weight", &slack_weight) != 0 ||
                parse_int_arg(argv[i + 2], "max consecutive grants", &streak_limit) != 0) {
                return 1;
            }
            i += 2;

            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ignored_aging_weight = streak_limit;
                if (parse_int_arg(argv[i + 1], "streak limit", &streak_limit) != 0) {
                    return 1;
                }
                fprintf(stderr,
                        "[WARN] legacy -s slack aging streak form detected; "
                        "ignoring aging_weight=%d\n",
                        ignored_aging_weight);
                i += 1;
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            if (i + 3 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            if (parse_positive_uint(argv[i + 1], "resnet deadline ms", &stats[MODEL_RESNET].deadline_ms) != 0 ||
                parse_positive_uint(argv[i + 2], "mobilenet deadline ms", &stats[MODEL_MOBILENET].deadline_ms) != 0 ||
                parse_positive_uint(argv[i + 3], "bitnet deadline ms", &stats[MODEL_BITNET].deadline_ms) != 0) {
                return 1;
            }
            i += 3;
        } else if (strcmp(argv[i], "-p") == 0) {
            if (i + 3 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            if (parse_positive_uint(argv[i + 1], "resnet period ms", &stats[MODEL_RESNET].period_ms) != 0 ||
                parse_positive_uint(argv[i + 2], "mobilenet period ms", &stats[MODEL_MOBILENET].period_ms) != 0 ||
                parse_positive_uint(argv[i + 3], "bitnet period ms", &stats[MODEL_BITNET].period_ms) != 0) {
                return 1;
            }
            period_was_set = 1;
            i += 3;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--max-bitnet-jobs") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            if (parse_positive_uint(argv[i + 1],
                                    "max bitnet jobs",
                                    &max_bitnet_jobs) != 0) {
                return 1;
            }
            i += 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[ERROR] unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!period_was_set) {
        for (int i = 0; i < MODEL_COUNT; i++) {
            stats[i].period_ms = stats[i].deadline_ms;
        }
    }

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    EVAL_PRINT( "[INFO] starting bitnet server before workload\n");
    if (check_bitnet_health() != 0) {
        if (spawn_bitnet_server() != 0 || wait_for_bitnet_ready(600) != 0) {
            stop_bitnet_server();
            return 1;
        }
    } else {
        fprintf(stderr, "[INFO] existing bitnet server is already ready\n");
    }

    if (syscall(NV_SCHEDULER_INIT,
                slack_weight,
                0,
                streak_limit) < 0) {
        fprintf(stderr, "[ERROR] nv_scheduler_init failed: %s\n", strerror(errno));
        stop_bitnet_server();
        return 1;
    }

    if (check_executable(RESNET_PATH) != 0 ||
        check_executable(MOBILENET_PATH) != 0) {
        stop_bitnet_server();
        return 1;
    }

    fprintf(stderr,
            "[INFO] scheduler params: slack_weight=%d max_consecutive_grants=%d max_bitnet_jobs=%u\n",
            slack_weight,
            streak_limit,
            max_bitnet_jobs);
    for (int i = 0; i < MODEL_COUNT; i++) {
        int rc;
        releaser_args[i].model_id = (model_id_t)i;
        rc = pthread_create(&releasers[i], NULL, release_loop, &releaser_args[i]);
        if (rc != 0) {
            fprintf(stderr, "[ERROR] pthread_create(%s releaser) failed: %s\n",
                    stats[i].name, strerror(rc));
            stop_requested = 1;
            exit_code = 1;
            for (int j = 0; j < i; j++) {
                (void)pthread_join(releasers[j], NULL);
            }
            wait_for_active_jobs();
            print_stats();
            stop_bitnet_server();
            return exit_code;
        }
    }

    for (int i = 0; i < MODEL_COUNT; i++) {
        void *ret = NULL;
        if (pthread_join(releasers[i], &ret) != 0 || ret != NULL) {
            exit_code = 1;
        }
    }

    wait_for_active_jobs();
    print_stats();
    stop_bitnet_server();
    return exit_code;
}
