/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
static void lifecycle_case(const char *name) {
    __ASSERT_NO_MSG(device_is_ready(matrix) && device_is_ready(output));
    struct zip_matrix_runtime_params p=parameters();
    p.enabled=true; p.long_press_ms=0;
    apply(p);
    const struct device *source=DEVICE_DT_GET(DT_NODELABEL(matrix_source_left));
    const struct device *right=DEVICE_DT_GET(DT_NODELABEL(matrix_source_right));
    if (!strncmp(name,"route-",6)) {
        if (!strcmp(name,"route-suppressed-release") || !strcmp(name,"route-touch-alias")) {
            p.enabled=false; apply(p); source_policy(false);
            __ASSERT_NO_MSG(input_report_key(right,INPUT_BTN_0,1,true,K_FOREVER)==0);
            k_msleep(5);
            p.enabled=true; apply(p); source_policy(true);
            bool touch=!strcmp(name,"route-touch-alias");
            actual_input(touch?INPUT_BTN_TOUCH:INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(2,false)>=0);
            actual_input(touch?INPUT_BTN_TOUCH:INPUT_BTN_0,0);
            bool retained=zmk_hid_get_mouse_report()->body.buttons&1U;
            printk("CLICK OBSERVATION case=%s early_release=%u time=%lld\n",
                   name,!retained,k_uptime_get());
            __ASSERT_NO_MSG(input_report_key(right,INPUT_BTN_0,0,true,K_FOREVER)==0);
            k_msleep(5);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            if(touch) {
                /* This is still the suppressed gesture contact's delayed tap.
                 * Leaving the route alone does not start a new contact. */
                unsigned before=mouse_sends;
                actual_input(INPUT_BTN_0,1); actual_input(INPUT_BTN_0,0);
                __ASSERT_NO_MSG(mouse_sends==before &&
                                !(zmk_hid_get_mouse_report()->body.buttons&1U));
                /* A complete fresh pointer contact clears only its contact mask. */
                actual_input(INPUT_BTN_TOUCH,1);
                __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
                actual_input(INPUT_BTN_TOUCH,0);
                __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
                printk("CONTACT RECOVERY delayed_pair=silent fresh_contact=complete\n");
            }
            actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
            actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            printk("CLICK RECOVERY case=%s buttons=0 fresh_pair=complete\n",name);
            if(strict_clicks) __ASSERT(retained,"another source was released early");
        } else if (!strcmp(name,"route-forwarded-pairs")) {
            p.enabled=false; apply(p); source_policy(false);
            actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(2,false)>=0);
            actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
            actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(2,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(1,false)>=0);
            actual_input(INPUT_BTN_0,1);
            bool reentry_forwarded=zmk_hid_get_mouse_report()->body.buttons&1U;
            printk("CLICK OBSERVATION case=%s reentry_click_forwarded=%u time=%lld\n",
                   name,reentry_forwarded,k_uptime_get());
            actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
            actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            printk("CLICK RECOVERY case=%s buttons=0 fresh_pair=complete\n",name);
            if(strict_clicks) __ASSERT(reentry_forwarded,"first click after route reentry was lost");
        } else if (!strcmp(name,"route-forwarded-repeat")) {
            p.enabled=false; apply(p); source_policy(false);
            actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(2,false)>=0);
            actual_input(INPUT_BTN_0,1); actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(2,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(1,false)>=0);
            actual_input(INPUT_BTN_0,1); actual_input(INPUT_BTN_0,0);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
        } else {
            __ASSERT_NO_MSG(!strcmp(name,"route-cancels-hold"));
            __ASSERT_NO_MSG(kscan_config(output,observe_a)==0);
            __ASSERT_NO_MSG(kscan_enable_callback(output)==0);
            p.long_press_ms=20; apply(p);
            contact(0,100); k_msleep(30);
            __ASSERT_NO_MSG(count==1 && views[0]);
            __ASSERT_NO_MSG(zmk_keymap_layer_deactivate(1,false)>=0);
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(2,false)>=0);
            actual_input(INPUT_BTN_TOUCH,0); k_msleep(30);
            __ASSERT_NO_MSG(count==2 && !views[0]);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
        }
    } else if (!strncmp(name,"frame-",6)) {
        apply(p);
        unsigned before=mouse_sends;
        if (!strcmp(name,"frame-empty-suppressed")) {
            actual_input(INPUT_BTN_TOUCH,1);
            __ASSERT_NO_MSG(input_report_abs(source,INPUT_ABS_X,100,false,K_FOREVER)==0);
            __ASSERT_NO_MSG(input_report_abs(source,INPUT_ABS_Y,100,true,K_FOREVER)==0);
            k_msleep(5); actual_input(INPUT_BTN_TOUCH,0);
            __ASSERT_NO_MSG(mouse_sends==before);
        } else {
            bool key_release=!strcmp(name,"frame-suppressed-release");
            if(key_release) actual_input(INPUT_BTN_0,1);
            __ASSERT_NO_MSG(input_report_rel(source,INPUT_REL_X,7,false,K_FOREVER)==0);
            if (!strcmp(name,"frame-settings-reset")) apply(p);
            if (!strcmp(name,"frame-layer-reset")) {
                __ASSERT_NO_MSG(zmk_keymap_layer_activate(3,false)>=0);
            }
            if (!strcmp(name,"frame-suppressed-abs")) {
                __ASSERT_NO_MSG(input_report_abs(source,INPUT_ABS_X,100,true,K_FOREVER)==0);
                k_msleep(5);
            } else {
                actual_input(INPUT_BTN_0,key_release?0:1);
            }
            __ASSERT_NO_MSG(mouse_sends==before+1 && last_mouse_x==7);
            __ASSERT_NO_MSG(!(zmk_hid_get_mouse_report()->body.buttons&1U));
        }
    } else if(!strcmp(name,"final-layer-repeat") || !strcmp(name,"final-layer-no-repeat-control") ||
       !strcmp(name,"final-no-layer-repeat-control")) {
        p.enabled=false; apply(p); source_policy(false);
        actual_input(INPUT_BTN_0,1);
        if(strcmp(name,"final-no-layer-repeat-control")) {
            __ASSERT_NO_MSG(zmk_keymap_layer_activate(3,false)>=0);
        }
        if(strcmp(name,"final-layer-no-repeat-control")) actual_input(INPUT_BTN_0,1);
        actual_input(INPUT_BTN_0,0);
        __ASSERT(!(zmk_hid_get_mouse_report()->body.buttons&1U),
                 "layer change allows duplicate mouse owner and leaves button held");
    } else if(!strcmp(name,"final-layer-suppressed-other-owner")) {
        p.enabled=false; apply(p); source_policy(false);
        __ASSERT_NO_MSG(input_report_key(right,INPUT_BTN_0,1,true,K_FOREVER)==0);
        k_msleep(5);
        __ASSERT_NO_MSG(zmk_hid_get_mouse_report()->body.buttons&1U);
        p.enabled=true; apply(p); source_policy(true);
        actual_input(INPUT_BTN_0,1); /* This left press is consumed. */
        __ASSERT_NO_MSG(zmk_keymap_layer_activate(3,false)>=0);
        actual_input(INPUT_BTN_0,0);
        printk("FINAL other source still pressed; buttons=%u time=%lld\n",
               zmk_hid_get_mouse_report()->body.buttons,k_uptime_get());
        __ASSERT(zmk_hid_get_mouse_report()->body.buttons&1U,
                 "layer change forwards a suppressed release and clears another owner");
        __ASSERT_NO_MSG(input_report_key(right,INPUT_BTN_0,0,true,K_FOREVER)==0);
        k_msleep(5);
    } else {
        __ASSERT_NO_MSG(!strcmp(name,"final-suppressed-frame") ||
                        !strcmp(name,"final-forwarded-frame-control"));
        p.enabled=!strcmp(name,"final-suppressed-frame"); apply(p);
        unsigned before=mouse_sends;
        __ASSERT_NO_MSG(input_report_rel(source,INPUT_REL_X,7,false,K_FOREVER)==0);
        actual_input(INPUT_BTN_0,1); /* Final synchronized event of this frame. */
        printk("FINAL frame sends=%u before=%u last_x=%d pending_x=%d time=%lld\n",
               mouse_sends,before,last_mouse_x,zmk_hid_get_mouse_report()->body.d_x,k_uptime_get());
        unsigned first_sends=mouse_sends;
        int16_t first_x=last_mouse_x;
        __ASSERT_NO_MSG(input_report_rel(source,INPUT_REL_X,3,true,K_FOREVER)==0);
        k_msleep(5);
        printk("FINAL next frame sends=%u first_sends=%u first_x=%d last_x=%d time=%lld\n",
               mouse_sends,first_sends,first_x,last_mouse_x,k_uptime_get());
        __ASSERT(first_sends==before+1 && first_x==7 && mouse_sends==before+2 && last_mouse_x==3,
                 "suppressed final key loses the pending motion frame boundary");
        actual_input(INPUT_BTN_0,0);
    }
    printk("matrix deep-scan PASS case=%s\n",name);
    exit(0);
}
