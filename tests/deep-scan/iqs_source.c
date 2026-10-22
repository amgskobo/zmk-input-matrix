/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
/* Include the selected producer unchanged. No copied click state machine and
 * no sensor DT instance: this fixture starts at the gesture-to-click handoff.
 * Unreferenced I2C/GPIO/PM functions are removed by normal section GC.
 */
#include MATRIX_TEST_IQS_SOURCE
#include "iqs_source.h"

struct source_fixture {
    struct iqs7211e_data data;
    struct k_work request;
    struct k_sem queued, resume, emitted;
    unsigned clicks;
    bool pause;
    struct matrix_iqs_edge edges[32];
    atomic_t count;
};
static struct source_fixture sources[2];

int __real_input_report(const struct device *, uint8_t, uint16_t, int32_t, bool,
                        k_timeout_t);
int __wrap_input_report(const struct device *dev, uint8_t type, uint16_t code,
                        int32_t value, bool sync, k_timeout_t timeout) {
    struct source_fixture *source = NULL;
    unsigned id = 0;
    for (; id < ARRAY_SIZE(sources); id++) {
        if (sources[id].data.dev == dev) { source = &sources[id]; break; }
    }
    if (source && type == INPUT_EV_KEY && code == INPUT_BTN_0) {
        __ASSERT_NO_MSG(k_current_get() == &iqs7211e_work_queue.thread);
        __ASSERT_NO_MSG(sync);
        unsigned index = atomic_get(&source->count);
        __ASSERT_NO_MSG(index < ARRAY_SIZE(source->edges));
        source->edges[index] = (struct matrix_iqs_edge){value, k_uptime_get()};
        printk("IQS SOURCE source=%u index=%u value=%d time=%lld private_queue=1\n",
               id, index, value, source->edges[index].ms);
        bool drop = IS_ENABLED(CONFIG_MATRIX_IQS_DROP_RELEASE) && value==0;
        if(drop) printk("IQS INJECTED dropped_release source=%u time=%lld\n",id,k_uptime_get());
        int ret = drop ? 0 : __real_input_report(dev, type, code, value, sync, timeout);
        __ASSERT_NO_MSG(ret == 0);
        atomic_inc(&source->count);
        k_sem_give(&source->emitted);
        return ret;
    }
    return __real_input_report(dev, type, code, value, sync, timeout);
}
static void request(struct k_work *work) {
    struct source_fixture *source = CONTAINER_OF(work, struct source_fixture, request);
    unsigned before = atomic_get(&source->count);
    bool owed_release = source->data.click_edges % 2;
    iqs7211e_queue_clicks(&source->data, INPUT_BTN_0, source->clicks);
    if (IS_ENABLED(CONFIG_MATRIX_IQS_INLINE_FIRST))
        iqs7211e_click_work_handler(&source->data.click_work.work);
    /* Only an old outstanding ON may be closed inline. The new ON must wait
     * for this same private queue to finish the current producer work. */
    __ASSERT(atomic_get(&source->count) == before + owed_release,
             "IQS first ON was emitted inline");
    printk("IQS QUEUED source=%u clicks=%u time=%lld pause=%u\n",
           (unsigned)(source-sources),source->clicks,k_uptime_get(),source->pause);
    k_sem_give(&source->queued);
    if (source->pause) __ASSERT_NO_MSG(k_sem_take(&source->resume,K_SECONDS(1))==0);
}
void matrix_iqs_init(void) {
    __ASSERT_NO_MSG(CONFIG_IQS7211E_WORKQUEUE_PRIORITY == -1);
    __ASSERT_NO_MSG(CONFIG_IQS7211E_WORKQUEUE_STACK_SIZE == 1536);
    (void)iqs7211e_init; /* No hardware DT instance in this boundary fixture. */
    iqs7211e_start_work_queue();
    for (unsigned i=0; i<ARRAY_SIZE(sources); i++) {
        struct source_fixture *s=&sources[i];
        s->data.dev=i ? DEVICE_DT_GET(DT_NODELABEL(matrix_source_right)) :
                       DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
        k_work_init_delayable(&s->data.click_work,iqs7211e_click_work_handler);
        k_work_init(&s->request,request);
        k_sem_init(&s->queued,0,1); k_sem_init(&s->resume,0,1);
        k_sem_init(&s->emitted,0,32);
    }
}
void matrix_iqs_start(unsigned id,unsigned clicks,bool pause) {
    __ASSERT_NO_MSG(id<ARRAY_SIZE(sources) && clicks>=1 && clicks<=3);
    struct source_fixture *s=&sources[id];
    s->clicks=clicks; s->pause=pause;
    __ASSERT_NO_MSG(iqs7211e_submit_work(&s->request)>=0);
    __ASSERT_NO_MSG(k_sem_take(&s->queued,K_SECONDS(1))==0);
}
void matrix_iqs_resume(unsigned id) { k_sem_give(&sources[id].resume); }
unsigned matrix_iqs_count(unsigned id) { return atomic_get(&sources[id].count); }
struct matrix_iqs_edge matrix_iqs_edge(unsigned id,unsigned index) {
    __ASSERT_NO_MSG(id<ARRAY_SIZE(sources) && index<matrix_iqs_count(id));
    return sources[id].edges[index];
}
void matrix_iqs_wait(unsigned id,unsigned edges) {
    while (matrix_iqs_count(id)<edges)
        __ASSERT_NO_MSG(k_sem_take(&sources[id].emitted,K_MSEC(300))==0);
}
