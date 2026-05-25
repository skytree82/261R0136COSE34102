#include <linux/syscalls.h>
#include <linux/nv_interceptor.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/wait.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/string.h>
#include <linux/irqflags.h>
#include <linux/errno.h>

#define NS_PER_MS        1000000ULL
#define DEFAULT_SLACK_WEIGHT 1
#define DEFAULT_STREAK_LIMIT 10
/*
 * The NPU completion path tracks outstanding slices in npu_queue_info.gemmini_queue.
 * Keep the scheduler's notion of "full" aligned with the actual ring capacity;
 * otherwise a 4th enqueue would wrap a 3-entry ring and corrupt IRQ accounting.
 */
#define NPU_INFLIGHT_LIMIT NUM_MAX_GEMMINI_QUEUE
#define OVERDUE_BOOST    1000
#define MODEL_MUST_RESUME 1U

#define WEIGHT_MIN 0
#define WEIGHT_MAX 1000
#define STREAK_LIMIT_MIN 0
#define STREAK_LIMIT_MAX 1000

atomic64_t log_seq = ATOMIC64_INIT(0);

#ifndef PRINT_ENABLE
#define PRINT_ENABLE 0
#endif

#if PRINT_ENABLE
#define NV_PRINTK(...) MY_PRINTK(__VA_ARGS__)
#else
#define NV_PRINTK(...) do { if (0) printk(__VA_ARGS__); } while (0)
#endif

extern int debug_sret_pid;
extern int debug_sret_arm;

model_table_t model_table;
npu_queue_info_t npu_queue_info;
struct mutex nv_sched_lock;
bool nv_retry_kick_for_npu;
bool nv_retry_kick_for_vpu;

static int last_scheduled_model_id = -1;    // For consecutive grant tracking for streak avoidance
static int issue_owner[DEV_COUNT] = {
    [DEV_NONE] = -1,
    [DEV_NPU] = -1,
    [DEV_VPU] = -1,
};
static int runtime_slack_weight = DEFAULT_SLACK_WEIGHT;
static int runtime_streak_limit = DEFAULT_STREAK_LIMIT;

static int issue_dev_is_valid(enum dev_type dev)
{
    return dev > DEV_NONE && dev < DEV_COUNT;
}

static int issue_owner_busy(enum dev_type dev)
{
    if (!issue_dev_is_valid(dev))
        return 0;

    return READ_ONCE(issue_owner[dev]) >= 0;
}

static void nv_set_issue_owner(enum dev_type dev, int model_id)
{
    if (!issue_dev_is_valid(dev))
        return;

    WRITE_ONCE(issue_owner[dev], model_id);
    NV_PRINTK(KERN_INFO "Issue owner set: dev=%d model ID=%d\n", dev, model_id);
}

void nv_release_issue_owner(uint32_t model_id, enum dev_type dev)
{

    NV_PRINTK(KERN_INFO "OWNER_RELEASE_REQ model=%u dev=%d current_owner=%d\n",
       model_id, dev, READ_ONCE(issue_owner[dev]));

    if (!issue_dev_is_valid(dev))
        return;

    if (READ_ONCE(issue_owner[dev]) == (int)model_id) {
        WRITE_ONCE(issue_owner[dev], -1);
        NV_PRINTK(KERN_INFO "Issue owner released: dev=%d model ID=%u\n",
            dev, model_id);
    }
}


static void mark_model_dispatched(int selected_idx)
{
    model_ctx_t *ctx = &model_table.running_models[selected_idx];

    if (last_scheduled_model_id == selected_idx)
        ctx->consecutive_grants++;
    else
        ctx->consecutive_grants = 1;

    ctx->total_grants++;

    last_scheduled_model_id = selected_idx;
}

static int model_is_overdue(model_ctx_t *model_ctx, uint64_t now_ns)
{
    return (int64_t)model_ctx->deadline - (int64_t)now_ns < 0;
}

static int64_t model_slack_ns(model_ctx_t *model_ctx, uint64_t now_ns)
{
    return (int64_t)model_ctx->deadline - (int64_t)now_ns;
}

static int64_t model_slack_ns_to_ms(int64_t slack_ns)
{
    return slack_ns / (int64_t)NS_PER_MS;
}

static int model_is_runnable_on_dev(model_ctx_t *model_ctx, enum dev_type dev)
{
    // 모델이 원하는 디바이스가 맞는지
    // TODO : NPU-VPU 전환도 고려해야 함.
    if (model_ctx->dev != dev)
        return 0;

    // 이미 실행 중인 모델은 다시 스케줄링하지 않는다.
    if (READ_ONCE(model_ctx->wait_flag) != 0)
        return 0;

    // VPU 명령어는 in-flight 여부를 체크해서 이미 실행 중이면 스케줄링하지 않는다.
    // Select VPU 에서 리턴 시 nv_retry_kick_for_vpu 를 true 로 세팅.
    if (dev == DEV_VPU && model_ctx->in_flight_ops != 0)
        return 0;

    return 1;
}

static int model_priority_precedes(int candidate_idx, int selected_idx)
{
    model_ctx_t *candidate;
    model_ctx_t *selected;

    if (selected_idx < 0)
        return 1;

    candidate = &model_table.running_models[candidate_idx];
    selected = &model_table.running_models[selected_idx];

    if (candidate->priority != selected->priority)
        return candidate->priority < selected->priority;

    if (candidate->total_grants != selected->total_grants)
        return candidate->total_grants < selected->total_grants;

    return candidate_idx < selected_idx;
}

static int model_grants_precedes(int candidate_idx, int selected_idx)
{
    model_ctx_t *candidate;
    model_ctx_t *selected;

    if (selected_idx < 0)
        return 1;

    candidate = &model_table.running_models[candidate_idx];
    selected = &model_table.running_models[selected_idx];

    if (candidate->total_grants != selected->total_grants)
        return candidate->total_grants < selected->total_grants;

    if (candidate->priority != selected->priority)
        return candidate->priority < selected->priority;

    return candidate_idx < selected_idx;
}

static int model_should_yield_for_streak(int model_idx)
{
    model_ctx_t *model_ctx = &model_table.running_models[model_idx];
    int streak_limit = READ_ONCE(runtime_streak_limit);

    if (streak_limit <= 0)
        return 0;

    if (model_idx != last_scheduled_model_id)
        return 0;

    if (model_ctx->consecutive_grants < (unsigned int)streak_limit)
        return 0;

    return 1;
}

static int select_model_for_dev(enum dev_type dev)
{
    int selected_idx = -1;
    int yielded_idx = -1;
    int yield_target_idx = -1;

    for (int i = 0; i < NUM_MAX_MODELS; i++) {
        model_ctx_t *model_ctx = &model_table.running_models[i];

        // 해당 model ctx 가 사용중인지 판단.
        if (!READ_ONCE(model_ctx->in_use))
            continue;

        if (!model_is_runnable_on_dev(model_ctx, dev))
            continue;

        if (READ_ONCE(model_ctx->must_run))
            return i;
        
        // 연속 횟수가 설정치 이상이면 여기서 걸려서 패스됨
        if (model_should_yield_for_streak(i)) {
            yielded_idx = i;
            continue;
        }

        // 양보 타겟 고르기.
        if (model_grants_precedes(i, yield_target_idx))
            yield_target_idx = i;

        if (model_priority_precedes(i, selected_idx))
            selected_idx = i;
    }

    // 양보 대상 선택 로직. 양보 대상/피대상 선택되었을 경우 작동
    if (yielded_idx >= 0 && yield_target_idx >= 0) {
        model_ctx_t *yielded_ctx = &model_table.running_models[yielded_idx];
        model_ctx_t *target_ctx = &model_table.running_models[yield_target_idx];

        NV_PRINTK(KERN_INFO "Streak limit: yielding model %s (ID: %d, streak=%u, grants=%llu, limit=%d, priority=%lld) to %s (ID: %d, grants=%llu, priority=%lld)\n",
            yielded_ctx->model_name, yielded_ctx->model_id,
            yielded_ctx->consecutive_grants,
            (unsigned long long)yielded_ctx->total_grants,
            READ_ONCE(runtime_streak_limit), yielded_ctx->priority,
            target_ctx->model_name, target_ctx->model_id,
            (unsigned long long)target_ctx->total_grants,
            target_ctx->priority);

        return yield_target_idx;
    }

    // 양보 없이 정상 실행
    if (selected_idx >= 0)
        return selected_idx;

    return yielded_idx;
}

int NPU_is_full(void) {
    int count = READ_ONCE(npu_queue_info.count);

    return count >= NPU_INFLIGHT_LIMIT;
}


// 우선순위 기반 실행 가능한 VPU 명령어 선택
static int select_VPU(void) {
    // if (issue_owner_busy(DEV_VPU))
    //     return 0;

    int selected_idx = select_model_for_dev(DEV_VPU);

    // 선택되면 wakeup
    if (selected_idx >= 0) {
        model_ctx_t *model_ctx = &model_table.running_models[selected_idx];
        uint64_t dispatch_ts;
        int64_t slack_ns;
        int64_t slack_ms;

        mark_model_dispatched(selected_idx);
        dispatch_ts = ktime_get_ns();
        slack_ns = model_slack_ns(model_ctx, dispatch_ts);
#if PRINT_ENABLE
        slack_ms = model_slack_ns_to_ms(slack_ns);
#endif
        NV_PRINTK(KERN_INFO "[VPU] Scheduling model %s (ID: %d) with slack: %lldms, priority: %lld, grants=%llu\n",
            model_ctx->model_name, model_ctx->model_id, 
            slack_ms, model_ctx->priority,
            (unsigned long long)model_ctx->total_grants);

        WRITE_ONCE(model_ctx->must_run, 0);
        model_ctx->wait_flag = 1;
        model_ctx->dispatch_ts = dispatch_ts;
        model_ctx->in_flight_ops++;
        // nv_set_issue_owner(DEV_VPU, selected_idx);
        wake_up(&model_ctx->wq);

        return 1;
    }

    WRITE_ONCE(nv_retry_kick_for_vpu, true); // VPU 스케줄링 실패 시 재킥 플래그 세팅. ISR에서 이 플래그 보고 재킥할지 결정.

    return 0;
}


// 우선순위 기반 실행 가능한 NPU 명령어 선택
static int select_NPU(void) {

    if (issue_owner_busy(DEV_NPU)){
        int owner = READ_ONCE(issue_owner[DEV_NPU]);
        model_ctx_t *owner_ctx = &model_table.running_models[owner];
        NV_PRINTK(KERN_INFO "NPU busy with model %s (ID: %d); cannot schedule new command\n",
            owner_ctx->model_name, owner_ctx->model_id);

        WRITE_ONCE(nv_retry_kick_for_npu, true);   // ISR 에서 추후 kick 되도록

        return 0;
    }


    int selected_idx = select_model_for_dev(DEV_NPU);

    // 선택되면 wakeup
    if (selected_idx >= 0) {
        int ret;
        model_ctx_t *model_ctx = &model_table.running_models[selected_idx];
        uint64_t dispatch_ts;
        int64_t slack_ns;
        int64_t slack_ms;

        mark_model_dispatched(selected_idx);
        dispatch_ts = ktime_get_ns();
        slack_ns = model_slack_ns(model_ctx, dispatch_ts);
#if PRINT_ENABLE
        slack_ms = model_slack_ns_to_ms(slack_ns);
#endif
        NV_PRINTK(KERN_INFO "[NPU] Scheduling model %s (ID: %d) with slack: %lldms, priority: %lld, grants=%llu\n",
            model_ctx->model_name, model_ctx->model_id, 
            slack_ms, model_ctx->priority,
            (unsigned long long)model_ctx->total_grants);

        WRITE_ONCE(model_ctx->must_run, 0);
        WRITE_ONCE(model_ctx->wait_flag, 1);
        model_ctx->dispatch_ts = dispatch_ts;
        model_ctx->in_flight_ops++;
        nv_set_issue_owner(DEV_NPU, selected_idx);

        ret = enqueue_nv_command(selected_idx);
        // NPU full 인데 push 한 경우
        if (ret) {
            model_ctx->in_flight_ops--;
            WRITE_ONCE(model_ctx->wait_flag, 0);
            nv_release_issue_owner(selected_idx, DEV_NPU);
            WRITE_ONCE(nv_retry_kick_for_npu, true);
            return 0;
        }

        MY_PRINTK("enqueued. head=%d tail=%d count=%d\n",
            READ_ONCE(npu_queue_info.head), READ_ONCE(npu_queue_info.tail), READ_ONCE(npu_queue_info.count));
        
        wake_up(&model_ctx->wq);

        return 1;
    }

    WRITE_ONCE(nv_retry_kick_for_npu, true);   // ISR 에서 추후 kick 되도록
    return 0;
}

void nv_kick_scheduler(void)
{
    WRITE_ONCE(nv_retry_kick_for_npu, false);
    WRITE_ONCE(nv_retry_kick_for_vpu, false);

    update_priorities();

    if (!NPU_is_full())
        select_NPU();
    else {
        WRITE_ONCE(nv_retry_kick_for_npu, true);
        MY_PRINTK("NPU full\n");
    }

    select_VPU();
}


// 우선순위 업데이트 함수
void update_priorities(void) {

    uint64_t now_ns = ktime_get_ns();

    for (int i = 0; i < NUM_MAX_MODELS; i++) {
        model_ctx_t *model_ctx = &model_table.running_models[i];
        int64_t slack_ns;
        int64_t slack_ms;
        int64_t p;
        int slack_w = READ_ONCE(runtime_slack_weight);
        int streak_limit = READ_ONCE(runtime_streak_limit);

        if (!READ_ONCE(model_ctx->in_use))
            continue;

        // TODO : 이미 실행중인 모델은 우선순위 업데이트 할지말지 고민

        slack_ns = model_slack_ns(model_ctx, now_ns);
        slack_ms = model_slack_ns_to_ms(slack_ns);

        // 우선순위 계산. p가 작을수록 먼저 선택된다.
        p = ((int64_t)slack_w * slack_ns);

        if (model_is_overdue(model_ctx, now_ns)) {
            p -= OVERDUE_BOOST; // 데드라인 지난 모델은 우선순위 대폭 상승
        }

        model_ctx->priority = p;

        NV_PRINTK(KERN_INFO "Model %s: dev=%d wait_flag=%u inflight=%d slack=%lldms, streak=%u, grants=%llu, streak_limit=%d, priority=%lld (w: s=%d)\n",
            model_ctx->model_name, model_ctx->dev, model_ctx->wait_flag,
            model_ctx->in_flight_ops, slack_ms,
            model_ctx->consecutive_grants,
            (unsigned long long)model_ctx->total_grants,
            streak_limit, model_ctx->priority, slack_w);
    }
}

int enqueue_nv_command(int model_idx)
{
    // NPU 큐가 full이면 명령을 받지 않고 -ENOSPC 반환
    if (READ_ONCE(npu_queue_info.count) >= NUM_MAX_GEMMINI_QUEUE) {
        MY_PRINTK("NPU queue overflow prevented for model ID %d: count=%d capacity=%d\n",
            model_idx, READ_ONCE(npu_queue_info.count), NUM_MAX_GEMMINI_QUEUE);
        return -ENOSPC;
    }

    // 큐에 모델 ID 추가
    npu_queue_info.gemmini_queue[npu_queue_info.tail] = model_idx;
    WRITE_ONCE(npu_queue_info.tail, (READ_ONCE(npu_queue_info.tail) + 1) % NUM_MAX_GEMMINI_QUEUE);
    WRITE_ONCE(npu_queue_info.count, READ_ONCE(npu_queue_info.count) + 1);

    NV_PRINTK(KERN_INFO "Enqueued model ID %d to NPU queue. head=%d tail=%d\n", 
        model_idx, npu_queue_info.head, npu_queue_info.tail);

    return 0;
}

int dequeue_nv_command(void)
{
    int model_idx = -1;
    
    // NPU 큐가 비어있으면 -ENOENT 반환
    if (READ_ONCE(npu_queue_info.count) <= 0) {
        NV_PRINTK(KERN_WARNING "NPU queue is empty; cannot dequeue command\n");
        return -ENOENT;
    }

    // 큐에서 모델 ID 추출
    model_idx = npu_queue_info.gemmini_queue[npu_queue_info.head];
    WRITE_ONCE(npu_queue_info.head, (READ_ONCE(npu_queue_info.head) + 1) % NUM_MAX_GEMMINI_QUEUE);
    WRITE_ONCE(npu_queue_info.count, READ_ONCE(npu_queue_info.count) - 1);


    NV_PRINTK(KERN_INFO "Dequeued model ID %d from NPU queue. head=%d tail=%d\n", 
        model_idx, npu_queue_info.head, npu_queue_info.tail);

    // 모델 ID 반환
    return model_idx;
}


/*------------ Syscall Part --------------*/


SYSCALL_DEFINE3(nv_scheduler_init, int, slack_weight, int, ignored_aging_weight, int, streak_limit)
{
    (void)ignored_aging_weight;

    memset(&model_table, 0, sizeof(model_table));
    memset(&npu_queue_info, 0, sizeof(npu_queue_info));
    model_table.table_count = 0;
    last_scheduled_model_id = -1;
    mutex_init(&nv_sched_lock);
    for (int dev = 0; dev < DEV_COUNT; dev++)
        WRITE_ONCE(issue_owner[dev], -1);

    if (slack_weight < WEIGHT_MIN || slack_weight > WEIGHT_MAX ||
        streak_limit < STREAK_LIMIT_MIN || streak_limit > STREAK_LIMIT_MAX) {
        NV_PRINTK(KERN_ERR "nv_scheduler_init: invalid params s=%d streak_limit=%d\n",
            slack_weight, streak_limit);
        return -EINVAL;
    }
    WRITE_ONCE(runtime_slack_weight, slack_weight);
    WRITE_ONCE(runtime_streak_limit, streak_limit);

    NV_PRINTK(KERN_INFO "nv_scheduler_init: params applied s=%d streak_limit=%d\n",
        READ_ONCE(runtime_slack_weight),
        READ_ONCE(runtime_streak_limit));

    return 0;
}


SYSCALL_DEFINE3(model_sleep_and_wait, uint32_t, model_id, int, dev_type, uint32_t, sched_flags)
{
    model_ctx_t *model_ctx;
    enum dev_type requested_dev;
    enum dev_type prev_dev;

    MY_PRINTK("Syscall model_sleep_and_wait called with model_id=%u, dev_type=%d, sched_flags=0x%x\n",
        model_id, dev_type, sched_flags);
    NV_PRINTK(KERN_INFO "Model %d requests scheduling on device %d (flags=0x%x).\n",
        model_id, dev_type, sched_flags);

    // 모델 ID 유효성 검사
    if (model_id >= NUM_MAX_MODELS)
        return -EINVAL;

    if (dev_type <= DEV_NONE || dev_type >= DEV_COUNT)
        return -EINVAL;

    requested_dev = (enum dev_type)dev_type;

    model_ctx = &model_table.running_models[model_id];

    if (!READ_ONCE(model_ctx->in_use))
        return -EINVAL;

    mutex_lock(&nv_sched_lock);
    prev_dev = model_ctx->dev;
    NV_PRINTK(KERN_INFO "Model %d: previous dev=%d, requested dev=%d\n", model_id, prev_dev, requested_dev);
    nv_release_issue_owner(model_id, prev_dev);

    // 직전 op가 끝났다면 in-flight를 먼저 해제하고, 새 op를 runnable 상태로 등록한다.
    // 이전 op 가 VPU 명령어였을 경우만 in-flight 해제한다.
    // NPU 명령어의 in-flight ops 는 ISR 에서 관리하도록 한다.
    if (model_ctx->in_flight_ops > 0 && prev_dev == DEV_VPU)
        model_ctx->in_flight_ops--; 
    
    WRITE_ONCE(model_ctx->dev, requested_dev);
    WRITE_ONCE(model_ctx->wait_flag, 0);
    WRITE_ONCE(model_ctx->must_run,
        (requested_dev == DEV_NPU && (sched_flags & MODEL_MUST_RESUME)) ? 1 : 0);

    // 테이블 반영 이후 우선순위를 계산한다.
    // update_priorities();

    // 이전 실행 기기에 따른 다음 모델 선택
    // TODO : NPU + VPU idle 에 따라 상호 전환 로직 구현

    nv_kick_scheduler();

    mutex_unlock(&nv_sched_lock);

    // 모델의 wait queue에서 대기. wakeup 되면 다시 스케줄링 함수에서 선택될 수 있도록.
    int ret_wait = wait_event_interruptible(model_ctx->wq, READ_ONCE(model_ctx->wait_flag) == 1);

    if (ret_wait)
        return ret_wait;  

    // 이후는 실행 허가되었을 때의 로직

    MY_PRINTK("Model wokeup");
    // WRITE_ONCE(debug_sret_pid, current->pid);
    // WRITE_ONCE(debug_sret_arm, 1);

    // printk(KERN_INFO "Model %d: scheduling wokeup on device %d.\n",
    //     model_id, dev_type);
    
    // NV_PRINTK(KERN_INFO "Model %d: inflights: %d.\n",
    //     model_id, inflights);

    // TODO : NPU / VPU 무엇으로 실행할지를 return 으로 반환 (0 / 1)

    return 0;
}

SYSCALL_DEFINE0(empty_model_queue) {
    NV_PRINTK(KERN_INFO "Requests to empty its queue.\n");

    for (int i = 0; i < NUM_MAX_MODELS; i++) {
        model_ctx_t *model_ctx = &model_table.running_models[i];

        memset(model_ctx, 0, sizeof(*model_ctx));
    }
    model_table.table_count = 0;

    memset(issue_owner, -1, sizeof(issue_owner));

    memset(npu_queue_info.gemmini_queue, 0, sizeof(npu_queue_info.gemmini_queue));
    WRITE_ONCE(npu_queue_info.head, 0);
    WRITE_ONCE(npu_queue_info.tail, 0);
    WRITE_ONCE(npu_queue_info.count, 0);

    return 0;
}
