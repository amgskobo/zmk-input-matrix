/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk-input-matrix/matrix_runtime.h>
#if IS_ENABLED(CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#include <zmk-input-matrix/custom_settings.h>
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    foreign_matrix_setting,"matrix_test_foreign","enabled",ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
    ZMK_CUSTOM_SETTING_VALUE_BOOL(true),ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
    ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,ZMK_CUSTOM_SETTING_PERMISSION_SECURE,
    ZMK_CUSTOM_SETTING_NO_CONSTRAINT);
#endif

static const struct device *const matrix = DEVICE_DT_GET(DT_NODELABEL(matrix));
static const struct device *const output = DEVICE_DT_GET(DT_NODELABEL(matrix_kscan));
struct edge {
    uint32_t row, column;
    bool pressed;
    int64_t ms;
};
static struct edge edges[32];
static size_t count;
static struct k_spinlock lock;
K_SEM_DEFINE(blocked, 0, 1);
K_SEM_DEFINE(resume, 0, 1);
K_SEM_DEFINE(drained, 0, 1);

K_SEM_DEFINE(hold_entered,0,1);
K_SEM_DEFINE(hold_resume,0,1);
static atomic_t pause_hold;
void zip_matrix_test_hold_snapshot(void) {
    if(atomic_cas(&pause_hold,1,0)) {
        k_sem_give(&hold_entered);
        __ASSERT_NO_MSG(k_sem_take(&hold_resume,K_SECONDS(1))==0);
    }
}

static void record(const struct device *dev, uint32_t row, uint32_t col, bool pressed) {
    ARG_UNUSED(dev);
    k_spinlock_key_t key = k_spin_lock(&lock);
    __ASSERT_NO_MSG(count < ARRAY_SIZE(edges));
    edges[count++] = (struct edge){row, col, pressed, k_uptime_get()};
    k_spin_unlock(&lock, key);
}
static void block(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&blocked);
    __ASSERT_NO_MSG(k_sem_take(&resume, K_SECONDS(1)) == 0);
}
static void barrier(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&drained);
}
K_WORK_DEFINE(blocker, block);
K_WORK_DEFINE(fence, barrier);

static void event(uint8_t stream, uint16_t type, uint16_t code, int32_t value, bool sync) {
    struct input_event e = {.type = type, .code = code, .value = value, .sync = sync};
    struct zmk_input_processor_state s = {.input_device_index = stream};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(matrix, &e, 0, 0, &s) >= 0);
}
static void contact(uint8_t stream, int32_t x) {
    event(stream, INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);
    event(stream, INPUT_EV_ABS, INPUT_ABS_X, x, false);
    event(stream, INPUT_EV_ABS, INPUT_ABS_Y, 100, true);
}
static void release(uint8_t stream) { event(stream, INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, true); }
static void expect(size_t n, uint32_t col, bool pressed) {
    __ASSERT_NO_MSG(count > n && edges[n].row == 0 && edges[n].column == col &&
                    edges[n].pressed == pressed);
}
static void clear(void) {
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = true;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    __ASSERT_NO_MSG(k_work_submit(&fence) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&drained, K_SECONDS(1)) == 0);
    for (size_t i = 0; i < count; i++) {
        printk("edge %lld %u %u %u\n", edges[i].ms, edges[i].row, edges[i].column,
               edges[i].pressed);
    }
    count = 0;
}
static void stall(void) {
    __ASSERT_NO_MSG(k_work_submit(&blocker) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&blocked, K_SECONDS(1)) == 0);
}
static void unstall(void) {
    k_sem_give(&resume);
    __ASSERT_NO_MSG(k_work_submit(&fence) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&drained, K_SECONDS(1)) == 0);
}
#if IS_ENABLED(CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS)
K_SEM_DEFINE(settings_snapshot_ready,0,1);
K_SEM_DEFINE(settings_snapshot_resume,0,1);
K_SEM_DEFINE(settings_writers_done,0,2);
K_THREAD_STACK_DEFINE(settings_stack_a,4096);
K_THREAD_STACK_DEFINE(settings_stack_b,4096);
static struct k_thread settings_thread_a,settings_thread_b;
static const struct zmk_custom_setting *race_enabled,*race_delay;
static atomic_t pause_settings_read;
int __real_zmk_custom_setting_read(const struct zmk_custom_setting *,struct zmk_custom_setting_value *);
int __wrap_zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                  struct zmk_custom_setting_value *value) {
    int rc=__real_zmk_custom_setting_read(setting,value);
    if(rc==0 && setting==race_delay && k_current_get()==&settings_thread_a &&
       atomic_cas(&pause_settings_read,1,0)) {
        k_sem_give(&settings_snapshot_ready);
        __ASSERT_NO_MSG(k_sem_take(&settings_snapshot_resume,K_SECONDS(1))==0);
    }
    return rc;
}
static void settings_writer(void *which,void *b,void *c) {
    ARG_UNUSED(b); ARG_UNUSED(c);
    struct zmk_custom_setting_value value;
    const struct zmk_custom_setting *setting;
    if(which==NULL) { setting=race_enabled; value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_BOOL(false); }
    else { setting=race_delay; value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(80); }
    __ASSERT_NO_MSG(zmk_custom_setting_write(setting,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)==0);
    k_sem_give(&settings_writers_done);
}
static void test_concurrent_settings(void) {
    clear();
    race_enabled=zmk_custom_setting_find(ZMK_INPUT_MATRIX_SUBSYSTEM,"matrix.enabled");
    race_delay=zmk_custom_setting_find(ZMK_INPUT_MATRIX_SUBSYSTEM,"matrix.long_press_ms");
    __ASSERT_NO_MSG(race_enabled!=NULL && race_delay!=NULL);
    atomic_set(&pause_settings_read,1);
    k_thread_create(&settings_thread_a,settings_stack_a,K_THREAD_STACK_SIZEOF(settings_stack_a),
        settings_writer,NULL,NULL,NULL,1,0,K_NO_WAIT);
    __ASSERT_NO_MSG(k_sem_take(&settings_snapshot_ready,K_SECONDS(1))==0);
    k_thread_create(&settings_thread_b,settings_stack_b,K_THREAD_STACK_SIZEOF(settings_stack_b),
        settings_writer,(void *)1,NULL,NULL,1,0,K_NO_WAIT);
    /* Both writer threads outrank this test thread. B has now finished or
     * blocked in the adapter, after its registry value was committed. */
    struct zmk_custom_setting_value stored;
    __ASSERT_NO_MSG(zmk_custom_setting_read(race_delay,&stored)==0 && stored.int32_value==80);
    k_sem_give(&settings_snapshot_resume);
    __ASSERT_NO_MSG(k_sem_take(&settings_writers_done,K_SECONDS(1))==0);
    __ASSERT_NO_MSG(k_sem_take(&settings_writers_done,K_SECONDS(1))==0);
    struct zip_matrix_runtime_params params;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix,&params)==0 && !params.enabled && params.long_press_ms==80);
    printk("PASS: concurrent registry edits cannot reapply an older Matrix snapshot\n");
}

static void test_registry_peer_hold(void) {
    const struct device *peer=DEVICE_DT_GET(DT_NODELABEL(matrix_peer));
    clear();
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix,&p)==0);
    p.long_press_ms=20; __ASSERT_NO_MSG(zip_matrix_set_params(matrix,&p)==0);
    __ASSERT_NO_MSG(zip_matrix_set_params(peer,&p)==0);
    contact(0,100);
    const struct zmk_input_processor_state state={.input_device_index=0};
    struct input_event events[]={
        {.type=INPUT_EV_KEY,.code=INPUT_BTN_TOUCH,.value=1},
        {.type=INPUT_EV_ABS,.code=INPUT_ABS_X,.value=900},
        {.type=INPUT_EV_ABS,.code=INPUT_ABS_Y,.value=100,.sync=true}};
    for(size_t i=0;i<ARRAY_SIZE(events);i++) {
        struct zmk_input_processor_state copy=state;
        __ASSERT_NO_MSG(zmk_input_processor_handle_event(peer,&events[i],0,0,&copy)>=0);
    }
    k_msleep(30); __ASSERT_NO_MSG(count==2); expect(0,0,true); expect(1,1,true);
    struct zmk_custom_setting_value value=ZMK_CUSTOM_SETTING_VALUE_BOOL(false);
    __ASSERT_NO_MSG(zmk_custom_setting_write(&foreign_matrix_setting,&value,
        ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)==0);
    __ASSERT_NO_MSG(count==2);
    const struct zmk_custom_setting *setting=zmk_custom_setting_find(
        ZMK_INPUT_MATRIX_SUBSYSTEM,"matrix_peer.flick_threshold");
    __ASSERT_NO_MSG(setting!=NULL);
    value=(struct zmk_custom_setting_value)ZMK_CUSTOM_SETTING_VALUE_INT32(51);
    __ASSERT_NO_MSG(zmk_custom_setting_write(setting,&value,ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY)==0);
    __ASSERT_NO_MSG(count==3); expect(2,1,false);
    release(0); __ASSERT_NO_MSG(count==4); expect(3,0,false);
    printk("PASS: actual registry foreign and peer edits preserve the other held Matrix contact\n");
}
#endif

static void tests(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    __ASSERT_NO_MSG(device_is_ready(matrix) && device_is_ready(output));
    __ASSERT_NO_MSG(CONFIG_SYSTEM_WORKQUEUE_PRIORITY < 0);
    __ASSERT_NO_MSG(kscan_config(output, record) == 0);
    __ASSERT_NO_MSG(kscan_enable_callback(output) == 0);

    clear();
    int64_t start = k_uptime_get();
    contact(0, 100);
    contact(1, 900);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    expect(0, 0, true);
    expect(1, 1, true);
    __ASSERT_NO_MSG(edges[0].ms >= start + 20 && edges[1].ms >= start + 20);
    release(0);
    release(1);
    __ASSERT_NO_MSG(count == 4);
    expect(2, 0, false);
    expect(3, 1, false);
    printk("PASS: two independent real delayed holds release in order\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    release(0);
    unstall();
    __ASSERT_NO_MSG(count == 2);
    expect(0, 0, true);
    expect(1, 0, false);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    printk("PASS: cancel of an overdue queued hold emits one balanced tap only\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = false;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    unstall();
    k_msleep(30);
    __ASSERT_NO_MSG(count == 0);
    printk("PASS: runtime disable cancels overdue queued work without stale output\n");

    clear();
    contact(0, 100);
    contact(1, 900);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    p.enabled = false;
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
    __ASSERT_NO_MSG(count == 4);
    expect(2, 0, false);
    expect(3, 1, false);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 4);
    printk("PASS: settings disable balances both escaped hold presses before return\n");

    clear();
    contact(0, 100);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 1);
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(1, false) >= 0);
    __ASSERT_NO_MSG(count == 2);
    expect(1, 0, false);
    release(0);
    k_msleep(30);
    __ASSERT_NO_MSG(count == 2);
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1, false) >= 0);
    printk("PASS: real layer event releases the held cell exactly once\n");

    clear();
    stall();
    contact(0, 100);
    k_msleep(30);
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(1, false) >= 0);
    unstall();
    k_msleep(30);
    __ASSERT_NO_MSG(count == 0);
    __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1, false) >= 0);
    printk("PASS: real layer event cancels overdue pending hold\n");

    for(int scenario=0;scenario<3;scenario++) {
        clear();
        __ASSERT_NO_MSG(zip_matrix_get_params(matrix,&p)==0);
        p.long_press_ms=20; __ASSERT_NO_MSG(zip_matrix_set_params(matrix,&p)==0);
        atomic_set(&pause_hold,1); contact(0,100);
        __ASSERT_NO_MSG(k_sem_take(&hold_entered,K_SECONDS(1))==0);
        release(0); /* old handler is running but has not taken the contact lock */
        if(scenario!=2) {
            p.long_press_ms=scenario==0?0:80;
            __ASSERT_NO_MSG(zip_matrix_set_params(matrix,&p)==0);
        }
        count=0; int64_t replacement=k_uptime_get(); contact(0,900);
        k_sem_give(&hold_resume);
        __ASSERT_NO_MSG(k_work_submit(&fence)>=0);
        __ASSERT_NO_MSG(k_sem_take(&drained,K_SECONDS(1))==0);
        __ASSERT_NO_MSG(count==0);
        k_msleep(p.long_press_ms+10);
        if(p.long_press_ms) {
            __ASSERT_NO_MSG(count==1 && edges[0].ms>=replacement+p.long_press_ms);
            expect(0,1,true); release(0); __ASSERT_NO_MSG(count==2); expect(1,1,false);
        } else { __ASSERT_NO_MSG(count==0); }
        printk("PASS: running stale hold respects replacement deadline scenario=%d\n",scenario);
    }

#if IS_ENABLED(CONFIG_ZMK_INPUT_MATRIX_CUSTOM_SETTINGS)
    test_registry_peer_hold();
    test_concurrent_settings();
#endif
    for (size_t i = 0; i < count; i++) {
        printk("edge %lld %u %u %u\n", edges[i].ms, edges[i].row, edges[i].column,
               edges[i].pressed);
    }
    printk("matrix kernel boundary tests: PASS\n");
    exit(0);
}
K_THREAD_DEFINE(matrix_tests, 4096, tests, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                100);
