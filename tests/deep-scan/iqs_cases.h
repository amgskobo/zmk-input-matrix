/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include "iqs_source.h"

static void iqs_route(bool pointer) {
    if(pointer) {
        __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1,false)>=0);
        __ASSERT_NO_MSG(zmk_keymap_layer_activate(2,false)>=0);
    } else {
        __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(2,false)>=0);
        __ASSERT_NO_MSG(zmk_keymap_layer_activate(1,false)>=0);
    }
    printk("IQS ROUTE case=%s pointer=%u time=%lld\n",
           case_name,pointer,k_uptime_get());
}


static void iqs_sequence(unsigned source,unsigned pairs,bool replaced) {
    unsigned n=matrix_iqs_count(source);
    __ASSERT_NO_MSG(n==pairs*2);
    for(unsigned i=0;i<n;i++) {
        struct matrix_iqs_edge e=matrix_iqs_edge(source,i);
        __ASSERT_NO_MSG(e.value==(i%2==0));
        if(i && !(replaced && i<=2))
            __ASSERT_NO_MSG(e.ms-matrix_iqs_edge(source,i-1).ms>=20);
    }
}
static void iqs_case(const char *name) {
    struct zip_matrix_runtime_params p=parameters();
    p.enabled=true; p.long_press_ms=0; apply(p);
    __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
    bool overlap=strstr(name,"overlap")!=NULL;
    bool gate=strstr(name,"before-on")!=NULL;
    bool replace=strstr(name,"replace")!=NULL;
    bool to_matrix=strstr(name,"pointer-to-matrix")!=NULL;
    bool to_pointer=strstr(name,"matrix-to-pointer")!=NULL;
    bool returning=strstr(name,"pointer-return")!=NULL;
    bool pointer=to_matrix || returning || (!to_pointer && strstr(name,"pointer"));
    unsigned clicks=strstr(name,"triple")?3:strstr(name,"double")?2:1;
    iqs_route(pointer);
    matrix_iqs_init();
    iqs_capture=true;
    if(overlap) {
        matrix_iqs_start(0,1,false);
        matrix_iqs_wait(0,1);
        k_msleep(5); iqs_route(true);
        matrix_iqs_start(1,1,false);
        matrix_iqs_wait(1,2); matrix_iqs_wait(0,2);
        iqs_sequence(0,1,false); iqs_sequence(1,1,false);
    } else {
        matrix_iqs_start(1,clicks,gate);
        if(gate) {
            __ASSERT_NO_MSG(matrix_iqs_count(1)==0 && iqs_report_count==0);
            iqs_route(!pointer);
            k_msleep(5);
            __ASSERT_NO_MSG(matrix_iqs_count(1)==0);
            matrix_iqs_resume(1);
        } else {
            matrix_iqs_wait(1,1);
            if(to_matrix || to_pointer || returning || replace) k_msleep(5);
            if(to_matrix || returning) iqs_route(false);
            if(to_pointer) iqs_route(true);
            if(returning) { k_msleep(5); iqs_route(true); }
            if(replace) matrix_iqs_start(1,1,false);
        }
        matrix_iqs_wait(1,replace?4:clicks*2);
        iqs_sequence(1,replace?2:clicks,replace);
    }
    /* All actual producer OFFs and input callbacks complete before judging. */
    k_msleep(5);
    k_spinlock_key_t key=k_spin_lock(&iqs_trace_lock);
    iqs_capture=false;
    size_t reports=iqs_report_count;
    k_spin_unlock(&iqs_trace_lock,key);
    printk("IQS FINAL case=%s buttons=%u reports=%u time=%lld\n",name,
           zmk_hid_get_mouse_report()->body.buttons,(unsigned)reports,k_uptime_get());
    __ASSERT(!(zmk_hid_get_mouse_report()->body.buttons&1U),"IQS release left button held");
    if(overlap) {
        __ASSERT_NO_MSG(reports==2 && iqs_reports[0].buttons==1 && iqs_reports[1].buttons==0);
        __ASSERT_NO_MSG(iqs_reports[0].time<matrix_iqs_edge(0,1).ms &&
                        matrix_iqs_edge(0,1).ms<matrix_iqs_edge(1,1).ms &&
                        iqs_reports[1].time>=matrix_iqs_edge(1,1).ms);
        bool early=iqs_reports[1].time<matrix_iqs_edge(1,1).ms;
        printk("CLICK OBSERVATION case=%s early_release=%u time=%lld\n",
               name,early,k_uptime_get());
        iqs_capture=true;
        matrix_iqs_start(1,1,false);
        matrix_iqs_wait(1,4);
        k_msleep(5);
        key=k_spin_lock(&iqs_trace_lock);
        iqs_capture=false;
        __ASSERT_NO_MSG(iqs_report_count==4 && iqs_reports[2].buttons==1 &&
                        iqs_reports[3].buttons==0);
        k_spin_unlock(&iqs_trace_lock,key);
        __ASSERT_NO_MSG(matrix_iqs_edge(1,2).value==1 && matrix_iqs_edge(1,3).value==0);
        __ASSERT_NO_MSG(matrix_iqs_edge(1,3).ms-matrix_iqs_edge(1,2).ms>=20);
        __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
        printk("CLICK RECOVERY case=%s buttons=0 fresh_pair=complete\n",name);
        if(strict_clicks) __ASSERT(!early,"IQS other-source release ended click early");
    } else if(gate) {
        if(pointer) __ASSERT_NO_MSG(reports==0);
        else __ASSERT_NO_MSG(reports==2 && iqs_reports[0].buttons==1 &&
                              iqs_reports[1].buttons==0);
    } else if(pointer) {
        unsigned expected=to_matrix?2:replace?4:clicks*2;
        __ASSERT_NO_MSG(reports==expected);
        for(unsigned i=0;i<reports;i++)
            __ASSERT_NO_MSG(iqs_reports[i].buttons==(i%2==0));
        __ASSERT_NO_MSG(iqs_reports[reports-1].time>=
                        matrix_iqs_edge(1,to_matrix?1:replace?3:clicks*2-1).ms);
    } else {
        if(!to_pointer) __ASSERT_NO_MSG(reports==0);
        else {
            /* The suppressed first ON/OFF stays silent across route departure.
             * Only complete later pointer pairs produce reports. */
            __ASSERT_NO_MSG(reports==(clicks-1)*2);
            for(unsigned i=0;i<reports;i++) {
                __ASSERT_NO_MSG(iqs_reports[i].buttons==(i%2==0));
                __ASSERT_NO_MSG(iqs_reports[i].time>=matrix_iqs_edge(1,i+2).ms);
            }
        }
    }
    printk("matrix deep-scan PASS case=%s\n",name);
    exit(0);
}
