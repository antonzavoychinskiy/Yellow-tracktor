#include "telemetry_source.h"

#if !MODULE_SYNTHETIC_MODE

esp_err_t telemetry_source_init(void)
{
    return mavlink_bridge_init();
}

bool telemetry_source_poll_event(mavlink_bridge_event_t *out_event)
{
    return mavlink_bridge_poll_event(out_event);
}

bool telemetry_source_get_last_heartbeat(mavlink_heartbeat_info_t *out)
{
    return mavlink_bridge_get_last_heartbeat(out);
}

int64_t telemetry_source_last_heartbeat_age_ms(void)
{
    return mavlink_bridge_last_heartbeat_age_ms();
}

void telemetry_source_get_telemetry_snapshot(mavlink_telemetry_snapshot_t *out)
{
    mavlink_bridge_get_telemetry_snapshot(out);
}

void telemetry_source_send_heartbeat(void)
{
    mavlink_bridge_send_heartbeat();
}

void telemetry_source_send_arm_disarm(bool arm)
{
    mavlink_bridge_send_arm_disarm(arm);
}

void telemetry_source_send_set_mode(uint32_t custom_mode)
{
    mavlink_bridge_send_set_mode(custom_mode);
}

void telemetry_source_send_param_set_float(const char *param_id, float value)
{
    mavlink_bridge_send_param_set_float(param_id, value);
}

void telemetry_source_send_manual_control(int16_t x, int16_t y, int16_t z, int16_t r,
                                           uint16_t buttons)
{
    mavlink_bridge_send_manual_control(x, y, z, r, buttons);
}

#endif /* !MODULE_SYNTHETIC_MODE */
