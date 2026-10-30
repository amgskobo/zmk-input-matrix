/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_matrix

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/studio/custom.h>

#include <zmk-input-matrix/custom_settings.h>
#include <zmk-input-matrix/matrix_runtime.h>

#ifndef CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL
#define CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_matrix_input_processor_matrix_custom_settings, CONFIG_ZMK_INPUT_MATRIX_LOG_LEVEL);

static K_MUTEX_DEFINE(settings_lock);

static bool matrix_namespace_handler(const zmk_custom_CallRequest *request,
                                     pb_callback_t *encode_response);

static struct zmk_rpc_custom_subsystem_meta matrix_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-input-matrix"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

#define REGISTER_SUBSYSTEM(identifier, meta, handler)                                              \
    ZMK_RPC_CUSTOM_SUBSYSTEM(identifier, meta, handler)

REGISTER_SUBSYSTEM(ZMK_INPUT_MATRIX_SUBSYSTEM_TOKEN, &matrix_meta, matrix_namespace_handler);

static bool matrix_namespace_handler(const zmk_custom_CallRequest *request,
                                     pb_callback_t *encode_response)
{
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);
    return false;
}

#define MATRIX_BOOL_SETTING(n, field, key, default_value)                                         \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        matrix_cs_##field##_##n, ZMK_INPUT_MATRIX_SUBSYSTEM,                                      \
        ZMK_INPUT_MATRIX_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,                 \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(default_value),                                             \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

#define MATRIX_INT_SETTING(n, field, key, default_value, minimum, maximum)                         \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        matrix_cs_##field##_##n, ZMK_INPUT_MATRIX_SUBSYSTEM,                                      \
        ZMK_INPUT_MATRIX_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,                \
        ZMK_CUSTOM_SETTING_VALUE_INT32(default_value),                                             \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(minimum, maximum));

#define MATRIX_SETTINGS(n)                                                                         \
    ZMK_INPUT_MATRIX_ASSERT_NAME_FITS(n, "flick_threshold")                                   \
    MATRIX_BOOL_SETTING(n, enabled, "enabled", true)                                              \
    MATRIX_INT_SETTING(n, flick_threshold, "flick_threshold", DT_INST_PROP(n, flick_threshold),  \
                       1, UINT16_MAX)                                                               \
    MATRIX_INT_SETTING(n, long_press_ms, "long_press_ms", DT_INST_PROP(n, long_press_ms), 0,     \
                       UINT16_MAX)


DT_INST_FOREACH_STATUS_OKAY(MATRIX_SETTINGS)

static bool read_bool(const struct zmk_custom_setting *setting, bool *out)
{
    struct zmk_custom_setting_value value;
    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        return false;
    }
    *out = value.bool_value;
    return true;
}

static bool read_int32(const struct zmk_custom_setting *setting, int32_t *out)
{
    struct zmk_custom_setting_value value;
    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        return false;
    }
    *out = value.int32_value;
    return true;
}

#define MATRIX_APPLY(n)                                                                            \
    if (changed_setting == NULL || \
        changed_setting == &matrix_cs_enabled_##n || \
        changed_setting == &matrix_cs_flick_threshold_##n || \
        changed_setting == &matrix_cs_long_press_ms_##n) { \
        struct zip_matrix_runtime_params params;                                                   \
        int32_t flick_threshold;                                                                   \
        int32_t long_press_ms;                                                                     \
        if (read_bool(&matrix_cs_enabled_##n, &params.enabled) &&                                  \
            read_int32(&matrix_cs_flick_threshold_##n, &flick_threshold) &&                        \
            read_int32(&matrix_cs_long_press_ms_##n, &long_press_ms) &&                            \
            flick_threshold > 0 && flick_threshold <= UINT16_MAX && long_press_ms >= 0 &&          \
            long_press_ms <= UINT16_MAX) {                                                         \
            params.flick_threshold = (uint16_t)flick_threshold;                                    \
            params.long_press_ms = (uint16_t)long_press_ms;                                        \
            (void)zip_matrix_set_params(DEVICE_DT_INST_GET(n), &params);                           \
        }                                                                                          \
    }

static void matrix_apply_settings(const struct zmk_custom_setting *changed_setting)
{
    /* Concurrent notifications must not apply an older snapshot last. */
    k_mutex_lock(&settings_lock, K_FOREVER);
    DT_INST_FOREACH_STATUS_OKAY(MATRIX_APPLY)
    k_mutex_unlock(&settings_lock);
}

static int matrix_settings_event_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    /* Boot loads every node. An edit must not reset another node's live state. */
    matrix_apply_settings(changed == NULL ? NULL : changed->setting);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(matrix_custom_settings, matrix_settings_event_cb);
ZMK_SUBSCRIPTION(matrix_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(matrix_custom_settings, zmk_custom_settings_initialized);

static int matrix_check_unique_keys(void)
{
    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_MATRIX_SUBSYSTEM) != 0) {
            continue;
        }
        ZMK_CUSTOM_SETTING_FOREACH(other) {
            /* Report a pair once, from the second of the two: both point into
             * the one linker section, so their order is their address. */
            if (other < setting &&
                strcmp(other->custom_subsystem_id, ZMK_INPUT_MATRIX_SUBSYSTEM) == 0 &&
                strcmp(other->key, setting->key) == 0) {
                LOG_ERR("Duplicate matrix setting key: %s", setting->key);
            }
        }
    }
    return 0;
}

SYS_INIT(matrix_check_unique_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
