/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <string.h>
#include <zmk/hid.h>
#include <kscan_input_matrix.h>
#include <dt-bindings/zmk/keys.h>
#pragma push_macro("K_THREAD_DEFINE")
#undef K_THREAD_DEFINE
#define K_THREAD_DEFINE(...)
#include "../kernel/module/main.c"
#pragma pop_macro("K_THREAD_DEFINE")

static bool views[2];
static int callback_action;
static bool forward_to_keymap;
static const char *case_name;
static bool strict_clicks;
static struct zmk_kscan_matrix_claim concurrent_claims[3];
K_SEM_DEFINE(callback_entered, 0, 1);
K_SEM_DEFINE(callback_resume, 0, 1);
K_THREAD_STACK_DEFINE(producer_stack, 2048);
static struct k_thread producer_thread;
static void concurrent_producer(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    __ASSERT_NO_MSG(k_sem_take(&callback_entered, K_SECONDS(1)) == 0);
    __ASSERT_NO_MSG(zmk_kscan_matrix_acquire(output, &concurrent_claims[1], 0, 0) == 0);
    __ASSERT_NO_MSG(zmk_kscan_matrix_release(output, &concurrent_claims[0]) == 0);
    __ASSERT_NO_MSG(zmk_kscan_matrix_acquire(output, &concurrent_claims[2], 0, 1) == 0);
    __ASSERT_NO_MSG(zmk_kscan_matrix_release(output, &concurrent_claims[1]) == 0);
    __ASSERT_NO_MSG(zmk_kscan_matrix_release(output, &concurrent_claims[2]) == 0);
    k_sem_give(&callback_resume);
}

static unsigned mouse_sends;
static int16_t last_mouse_x;
#ifdef CONFIG_MATRIX_IQS_SOURCE_TEST
static bool iqs_capture;
static struct { uint32_t buttons; int64_t time; } iqs_reports[16];
static size_t iqs_report_count;
static struct k_spinlock iqs_trace_lock;
#endif
int __real_zmk_endpoint_send_mouse_report(void);
int __wrap_zmk_endpoint_send_mouse_report(void) {
    mouse_sends++;
    last_mouse_x=zmk_hid_get_mouse_report()->body.d_x;
#ifdef CONFIG_MATRIX_IQS_SOURCE_TEST
    k_spinlock_key_t key=k_spin_lock(&iqs_trace_lock);
    if(iqs_capture) {
        __ASSERT_NO_MSG(iqs_report_count<ARRAY_SIZE(iqs_reports));
        size_t i=iqs_report_count++;
        iqs_reports[i].buttons=zmk_hid_get_mouse_report()->body.buttons;
        iqs_reports[i].time=k_uptime_get();
        k_spin_unlock(&iqs_trace_lock,key);
        printk("IQS HID case=%s index=%u buttons=%u time=%lld\n",case_name,
               (unsigned)i,iqs_reports[i].buttons,iqs_reports[i].time);
    } else {
        k_spin_unlock(&iqs_trace_lock,key);
    }
#endif
    return __real_zmk_endpoint_send_mouse_report();
}

static struct zip_matrix_runtime_params parameters(void) {
    struct zip_matrix_runtime_params p;
    __ASSERT_NO_MSG(zip_matrix_get_params(matrix, &p) == 0);
    return p;
}
/* Composition scenarios choose output policy explicitly. Matrix's settings
 * setter never knows the final filter or changes this independent policy. */
static void source_policy(bool suppress) {
    int rc = suppress ? zmk_keymap_layer_deactivate(4, false)
                      : zmk_keymap_layer_activate(4, false);
    __ASSERT_NO_MSG(rc >= 0);
}
static void apply(struct zip_matrix_runtime_params p) {
    __ASSERT_NO_MSG(zip_matrix_set_params(matrix, &p) == 0);
}
static int button(uint8_t stream, uint16_t code, int32_t value) {
    struct input_event e = {.type=INPUT_EV_KEY,.code=code,.value=value,.sync=true};
    struct zmk_input_processor_state s = {.input_device_index=stream};
    int result=zmk_input_processor_handle_event(matrix,&e,0,0,&s);
    __ASSERT_NO_MSG(result == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(e.type == INPUT_EV_KEY && e.code == code && e.value == value && e.sync);
    printk("BUTTON case=%s stream=%u input=%u value=%d result=%d output=%u sync=%u time=%lld\n",
           case_name,stream,code,value,result,e.code,e.sync,k_uptime_get());
    return result;
}
static void observe_a(const struct device *dev, uint32_t row,
                    uint32_t col, bool pressed) {
    record(dev,row,col,pressed);
    __ASSERT_NO_MSG(row==0 && col<2);
    views[col]=pressed;
    printk("EDGE case=%s consumer=%u row=%u col=%u pressed=%u time=%lld\n",
           case_name,0U,row,col,pressed,k_uptime_get());
    if(forward_to_keymap) {
        __ASSERT_NO_MSG(zmk_keymap_position_state_changed(
            ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,row*2+col,pressed,k_uptime_get())>=0);
        printk("HID N0=%u N1=%u time=%lld\n",zmk_hid_is_pressed(N0),
               zmk_hid_is_pressed(N1),k_uptime_get());
    }
    if(pressed && callback_action) {
        int action=callback_action; callback_action=0;
        if(action==1) {
            struct zip_matrix_runtime_params p=parameters(); p.enabled=false; apply(p);
        } else if(action==2) {
            release(0); contact(0,900);
        } else if(action==3) {
            k_sem_give(&callback_entered);
            __ASSERT_NO_MSG(k_sem_take(&callback_resume,K_SECONDS(1))==0);
        } else {
            __ASSERT_NO_MSG(action==4);
            contact(1,100); release(1);
        }
    }
}
static void check_clean(void) { __ASSERT_NO_MSG(!views[0] && !views[1]); }
static void start_hold(uint8_t stream,int32_t x) { contact(stream,x); k_msleep(30); }

static void long_walk(void) {
    uint32_t random=0x43104d2bU;
    bool touch[2]={false,false};
    for(unsigned step=0;step<300;step++) {
        random^=random<<13; random^=random>>17; random^=random<<5;
        unsigned op=random%6, s=(random>>8)&1U;
        if(op==0 || op==5) { contact(s,s?900:100); touch[s]=true; }
        else if(op==1) { release(s); touch[s]=false; }
        else if(op==2) {
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(3,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(3,false)>=0);
            touch[0]=touch[1]=false;
        } else if(op==3) {
            struct zip_matrix_runtime_params p=parameters(); apply(p);
            touch[0]=touch[1]=false;
        } else {
            __ASSERT_NO_MSG(button(255,INPUT_BTN_TOUCH,1)==ZMK_INPUT_PROC_CONTINUE);
        }
        k_msleep(22);
        printk("STEP seed=43104d2b n=%u op=%u stream=%u expected=%u%u actual=%u%u time=%lld\n",
               step,op,s,touch[0],touch[1],views[0],views[1],k_uptime_get());
        __ASSERT_NO_MSG(views[0]==touch[0] && views[1]==touch[1]);
        count=0;
    }
    release(0); release(1); check_clean();
}
static void actual_input(uint16_t code,int32_t value) {
    const struct device *source=DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
    __ASSERT_NO_MSG(input_report_key(source,code,value,true,K_FOREVER)==0);
    k_msleep(5);
    printk("ACTUAL INPUT code=%u value=%d mouse_buttons=%u time=%lld\n",
           code,value,zmk_hid_get_mouse_report()->body.buttons,k_uptime_get());
}
static void secondary_event(uint16_t type,uint16_t code,int32_t value,bool sync) {
    const struct device *second=DEVICE_DT_GET(DT_NODELABEL(matrix_secondary));
    __ASSERT_NO_MSG(device_is_ready(second));
    struct input_event e={.type=type,.code=code,.value=value,.sync=sync};
    struct zmk_input_processor_state state={.input_device_index=0};
    __ASSERT_NO_MSG(zmk_input_processor_handle_event(second,&e,0,0,&state)>=0);
}
static void secondary_contact(int32_t x) {
    secondary_event(INPUT_EV_KEY,INPUT_BTN_TOUCH,1,false);
    secondary_event(INPUT_EV_ABS,INPUT_ABS_X,x,false);
    secondary_event(INPUT_EV_ABS,INPUT_ABS_Y,100,true);
}
#include "lifecycle_cases.h"
#ifdef CONFIG_MATRIX_IQS_SOURCE_TEST
#include "iqs_cases.h"
#endif

static void review(void *a,void *b,void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    (void)tests; /* Reuse setup/helpers, without registering the old test thread. */
    __ASSERT_NO_MSG(zmk_keymap_layer_activate(1,false)>=0);
    case_name=getenv("MATRIX_REVIEW_CASE");
    __ASSERT_NO_MSG(case_name!=NULL);
    strict_clicks=getenv("MATRIX_STRICT_CLICKS")!=NULL;
#ifdef CONFIG_MATRIX_IQS_SOURCE_TEST
    if(!strncmp(case_name,"iqs-",4)) iqs_case(case_name);
#endif
    if(!strncmp(case_name,"final-",6) || !strncmp(case_name,"route-",6) ||
       !strncmp(case_name,"frame-",6)) lifecycle_case(case_name);
    __ASSERT_NO_MSG(device_is_ready(matrix) && device_is_ready(output));
    __ASSERT_NO_MSG(kscan_config(output,observe_a)==0);
    __ASSERT_NO_MSG(kscan_enable_callback(output)==0);
    struct zip_matrix_runtime_params p=parameters();
    p.enabled=true; p.long_press_ms=20; apply(p);
    clear();
    forward_to_keymap=strstr(case_name,"real-hid")!=NULL || strstr(case_name,"separate-instances")!=NULL;
    if(!strcmp(case_name,"independent-filter-policy")) {
        p.enabled=false; apply(p);
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(4));
        unsigned before=mouse_sends;
        actual_input(INPUT_BTN_0,1); actual_input(INPUT_BTN_0,0);
        __ASSERT_NO_MSG(mouse_sends==before);
        source_policy(false);
        p.enabled=true; apply(p);
        __ASSERT_NO_MSG(zmk_keymap_layer_active(4));
        actual_input(INPUT_BTN_0,1);
        __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
        actual_input(INPUT_BTN_0,0); check_clean();
        printk("PASS: Matrix enable and final suppression policy are independent\n");
    } else if(!strcmp(case_name,"matrix-key-isolation")) {
        p.long_press_ms=0; apply(p);
        unsigned before=mouse_sends;
        const uint16_t codes[]={INPUT_BTN_0, INPUT_BTN_0+4, INPUT_BTN_TOUCH};
        for(size_t i=0; i<ARRAY_SIZE(codes); i++) {
            actual_input(codes[i],1); actual_input(codes[i],0);
            actual_input(codes[i],-1);
        }
        __ASSERT_NO_MSG(mouse_sends==before && count==0);
        const struct device *source=DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
        __ASSERT_NO_MSG(input_report_abs(source,INPUT_ABS_X,100,false,K_FOREVER)==0);
        __ASSERT_NO_MSG(input_report_abs(source,INPUT_ABS_Y,100,false,K_FOREVER)==0);
        actual_input(INPUT_BTN_TOUCH,1); actual_input(INPUT_BTN_TOUCH,0);
        __ASSERT_NO_MSG(mouse_sends==before && count==2);
        expect(0,0,true); expect(1,0,false); check_clean();
    } else if(!strcmp(case_name,"different-cells-real-hid")) {
        contact(0,100); contact(1,900); k_msleep(30);
        __ASSERT_NO_MSG(count==2 && views[0] && views[1]);
        __ASSERT_NO_MSG(zmk_hid_is_pressed(N0) && zmk_hid_is_pressed(N1));
        release(0); __ASSERT_NO_MSG(!views[0] && views[1]);
        __ASSERT_NO_MSG(!zmk_hid_is_pressed(N0) && zmk_hid_is_pressed(N1));
        release(1); check_clean(); __ASSERT_NO_MSG(count==4);
    } else if(!strcmp(case_name,"separate-instances-control") ||
              !strcmp(case_name,"separate-instances-same-cell")) {
        bool shared=!strcmp(case_name,"separate-instances-same-cell");
        contact(0,100); secondary_contact(shared?100:900); k_msleep(30);
        __ASSERT_NO_MSG(count==(shared?1:2) && views[0]);
        if(!shared) __ASSERT_NO_MSG(views[1] && zmk_hid_is_pressed(N0) && zmk_hid_is_pressed(N1));
        release(0);
        if(shared) {
            printk("OBSERVE second processor still holds; HID N0=%u\n",zmk_hid_is_pressed(N0));
            __ASSERT(zmk_hid_is_pressed(N0),"shared output released another processor's held key");
        }
        if(!shared) __ASSERT_NO_MSG(!zmk_hid_is_pressed(N0) && zmk_hid_is_pressed(N1));
        secondary_event(INPUT_EV_KEY,INPUT_BTN_TOUCH,0,true);
        __ASSERT_NO_MSG(count==(shared?2:4) && !zmk_hid_is_pressed(N0) && !zmk_hid_is_pressed(N1)); check_clean();
    } else if(!strcmp(case_name,"tap-no-hold")) {
        p.long_press_ms=0; apply(p); contact(0,100); release(0);
        __ASSERT_NO_MSG(count==2); expect(0,0,true); expect(1,0,false); check_clean();
    } else if(!strcmp(case_name,"disable-held")) {
        start_hold(0,100); __ASSERT_NO_MSG(views[0]);
        p.enabled=false; apply(p); check_clean();
        p.enabled=true; apply(p); k_msleep(30); __ASSERT_NO_MSG(count==2);
    } else if(!strcmp(case_name,"reentrant-disable")) {
        callback_action=1; start_hold(0,100);
        __ASSERT_NO_MSG(count==2); check_clean();
        __ASSERT_NO_MSG(!parameters().enabled);
    } else if(!strcmp(case_name,"reentrant-retouch")) {
        callback_action=2; start_hold(0,100);
        __ASSERT_NO_MSG(count==2); expect(0,0,true); expect(1,0,false); check_clean();
        k_msleep(25); __ASSERT_NO_MSG(count==3 && views[1]);
        expect(2,1,true); release(0); __ASSERT_NO_MSG(count==4); check_clean();
    } else if(!strcmp(case_name,"forwarded-release")) {
        p.enabled=false; apply(p);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        p.enabled=true; apply(p);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,0)==ZMK_INPUT_PROC_CONTINUE);
    } else if(!strcmp(case_name,"independent-button-release")) {
        p.enabled=true; apply(p);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        /* Stream1 may owe an up accepted on a route which bypassed Matrix.
         * Its up must not clear stream0's suppressed-pair record. */
        __ASSERT_NO_MSG(button(1,INPUT_BTN_0,0)==ZMK_INPUT_PROC_CONTINUE);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,0)==ZMK_INPUT_PROC_CONTINUE);
    } else if(!strcmp(case_name,"invalid-stream")) {
        start_hold(0,100); __ASSERT_NO_MSG(views[0]);
        __ASSERT_NO_MSG(button(255,INPUT_BTN_TOUCH,0)==ZMK_INPUT_PROC_CONTINUE);
        __ASSERT_NO_MSG(count==1 && views[0]); release(0); check_clean();
    } else if(!strcmp(case_name,"long-walk")) {
        long_walk();
    } else if(!strcmp(case_name,"same-cell-real-hid")) {
        contact(0,100); contact(1,100); k_msleep(30);
        __ASSERT_NO_MSG(count==1 && views[0]); release(0);
        printk("OBSERVE other stream still holds; output=%u\n",views[0]);
        __ASSERT(zmk_hid_is_pressed(N0),"real HID key cleared while another stream holds it");
        __ASSERT(views[0],"same-cell outstanding hold was released");
        __ASSERT_NO_MSG(count==1); release(1); check_clean();
        __ASSERT_NO_MSG(count==2);
        __ASSERT_NO_MSG(!zmk_hid_is_pressed(N0));
    } else if(!strcmp(case_name,"tap-over-hold-real-hid")) {
        start_hold(0,100); contact(1,100); release(1);
        __ASSERT_NO_MSG(count==1); expect(0,0,true);
        printk("OBSERVE stream0 hold overlaps stream1 tap; output=%u\n",views[0]);
        __ASSERT(zmk_hid_is_pressed(N0),"real HID key cleared by overlapping tap");
        __ASSERT(views[0],"tap released another stream's held cell");
        release(0); check_clean(); __ASSERT_NO_MSG(count==2);
        __ASSERT_NO_MSG(!zmk_hid_is_pressed(N0));
    } else if(!strcmp(case_name,"shared-disable-instance") || !strcmp(case_name,"shared-layer-reset")) {
        forward_to_keymap=true;
        contact(0,100); secondary_contact(100); k_msleep(30);
        __ASSERT_NO_MSG(count==1 && zmk_hid_is_pressed(N0));
        if(!strcmp(case_name,"shared-disable-instance")) {
            p.enabled=false; apply(p);
            __ASSERT_NO_MSG(count==1 && zmk_hid_is_pressed(N0));
            secondary_event(INPUT_EV_KEY,INPUT_BTN_TOUCH,0,true);
        } else {
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(3,false)>=0);
        }
        __ASSERT_NO_MSG(count==2 && !zmk_hid_is_pressed(N0)); check_clean();
    } else if(!strcmp(case_name,"tap-during-press-callback")) {
        callback_action=4; start_hold(0,100);
        __ASSERT_NO_MSG(count==1 && views[0]); release(0);
        __ASSERT_NO_MSG(count==2); check_clean();
    } else if(!strcmp(case_name,"concurrent-output")) {
        forward_to_keymap=true; callback_action=3;
        k_tid_t tid=k_thread_create(&producer_thread,producer_stack,K_THREAD_STACK_SIZEOF(producer_stack),
                                   concurrent_producer,NULL,NULL,NULL,1,0,K_NO_WAIT);
        __ASSERT_NO_MSG(zmk_kscan_matrix_acquire(output,&concurrent_claims[0],0,0)==0);
        __ASSERT_NO_MSG(k_thread_join(tid,K_SECONDS(1))==0);
        __ASSERT_NO_MSG(count==4); expect(0,0,true); expect(1,1,true);
        expect(2,0,false); expect(3,1,false); check_clean();
        __ASSERT_NO_MSG(!zmk_hid_is_pressed(N0) && !zmk_hid_is_pressed(N1));
    } else if(!strcmp(case_name,"repeat-one") || !strcmp(case_name,"repeat-two")) {
        p.enabled=false; apply(p);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        p.enabled=true; apply(p);
        int value=!strcmp(case_name,"repeat-two")?2:1;
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,value)==ZMK_INPUT_PROC_CONTINUE);
        int result=button(0,INPUT_BTN_0,0);
        __ASSERT(result==ZMK_INPUT_PROC_CONTINUE,
                 "forwarded button release consumed after repeated press");
    } else if(!strcmp(case_name,"touch-toggle")) {
        p.enabled=false; apply(p);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_TOUCH,1)==ZMK_INPUT_PROC_CONTINUE);
        p.enabled=true; apply(p);
        int result=button(0,INPUT_BTN_TOUCH,0);
        __ASSERT(result==ZMK_INPUT_PROC_CONTINUE,"forwarded touch release consumed after policy change");
    } else if(!strcmp(case_name,"forwarded-release-real-input") ||
              !strcmp(case_name,"repeat-real-input") || !strcmp(case_name,"touch-real-input")) {
        bool touch=!strcmp(case_name,"touch-real-input");
        uint16_t code=touch?INPUT_BTN_TOUCH:INPUT_BTN_0;
        p.enabled=false; apply(p); source_policy(false);
        actual_input(code,1);
        __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons & 1U);
        p.enabled=true; apply(p); source_policy(true);
        if(!strcmp(case_name,"repeat-real-input")) actual_input(code,1);
        actual_input(code,0);
        unsigned buttons=zmk_hid_get_mouse_report()->body.buttons;
        printk("ACTUAL INPUT FINAL buttons=%u\n",buttons);
        if(touch) __ASSERT(!(buttons&1U),"real mouse button remains held after touch policy change");
        else __ASSERT(!(buttons&1U),"real mouse button remains held after repeated input");
    } else if(!strcmp(case_name,"repeat-passthrough-real-input") ||
              !strcmp(case_name,"disabled-button-real-input")) {
        p.enabled=false; apply(p); source_policy(false);
        actual_input(INPUT_BTN_0,1);
        if(!strcmp(case_name,"disabled-button-real-input")) { p.enabled=false; apply(p); source_policy(false); }
        actual_input(INPUT_BTN_0,1); actual_input(INPUT_BTN_0,1);
        if(!p.enabled) { p.enabled=true; p.enabled=true; apply(p); source_policy(true); }
        actual_input(INPUT_BTN_0,0);
        __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
    } else if(!strcmp(case_name,"suppressed-pair-policy-edit")) {
        p.enabled=true; apply(p); source_policy(true);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        p.enabled=false; apply(p); source_policy(false);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,0)==ZMK_INPUT_PROC_CONTINUE);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,1)==ZMK_INPUT_PROC_CONTINUE);
        __ASSERT_NO_MSG(button(0,INPUT_BTN_0,0)==ZMK_INPUT_PROC_CONTINUE);
    } else if(!strcmp(case_name,"repeat-frame-real-input")) {
        p.enabled=false; apply(p); source_policy(false);
        actual_input(INPUT_BTN_0,1);
        unsigned before=mouse_sends;
        const struct device *source=DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
        __ASSERT_NO_MSG(input_report_rel(source,INPUT_REL_X,7,false,K_FOREVER)==0);
        actual_input(INPUT_BTN_0,1);
        __ASSERT_NO_MSG(mouse_sends==before+1 && last_mouse_x==7);
        actual_input(INPUT_BTN_0,0);
        __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
    } else if(!strcmp(case_name,"independent-mouse-owners") ||
              !strcmp(case_name,"touch-button-alias-real-input")) {
        p.enabled=false; apply(p); source_policy(false);
        bool alias=!strcmp(case_name,"touch-button-alias-real-input");
        const struct device *second=DEVICE_DT_GET(DT_NODELABEL(matrix_source_right));
        actual_input(alias?INPUT_BTN_TOUCH:INPUT_BTN_0,1);
        if(alias) actual_input(INPUT_BTN_0,1);
        else {
            __ASSERT_NO_MSG(input_report_key(second,INPUT_BTN_0,1,true,K_FOREVER)==0);
            k_msleep(5);
        }
        p.enabled=true; apply(p); source_policy(true);
        actual_input(alias?INPUT_BTN_TOUCH:INPUT_BTN_0,1);
        actual_input(alias?INPUT_BTN_TOUCH:INPUT_BTN_0,0);
        __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
        if(alias) actual_input(INPUT_BTN_0,0);
        else {
            __ASSERT_NO_MSG(input_report_key(second,INPUT_BTN_0,0,true,K_FOREVER)==0);
            k_msleep(5);
        }
        __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
    } else {
        __ASSERT(false,"unknown matrix deep-scan case");
    }
    printk("matrix deep-scan PASS case=%s\n",case_name);
    exit(0);
}
K_THREAD_DEFINE(matrix_deep_scan,4096,review,NULL,NULL,NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO,0,100);
