#include "simulator.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ftinspect/protocol.h"
#include "ftinspect/stream_decoder.h"

#define SIM_SESSION 0x20261020u

static int prepare_job(fi_controller_t *controller, uint32_t job_id)
{
    fi_controller_reset(controller, SIM_SESSION, 1000u, 2000u);
    if (fi_controller_arrive(controller, SIM_SESSION, job_id, 100u) != FI_CONTROL_OK) {
        return 1;
    }
    if (fi_controller_begin_stabilization(controller) != FI_CONTROL_OK) {
        return 1;
    }
    return fi_controller_request_capture(controller) == FI_CONTROL_OK ? 0 : 1;
}

static fi_control_result_t deliver_result(
    fi_controller_t *controller,
    fi_stream_decoder_t *decoder,
    uint32_t job_id,
    uint32_t seq,
    fi_route_t route,
    uint32_t now_ms,
    int corrupt_crc,
    fi_sim_report_t *report)
{
    fi_frame_t input;
    fi_frame_t decoded;
    fi_status_t decode_status = FI_OK;
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t length = 0u;
    size_t i;

    memset(&input, 0, sizeof(input));
    input.type = FI_MSG_RESULT;
    input.session_id = SIM_SESSION;
    input.job_id = job_id;
    input.seq = seq;
    input.payload_length = 1u;
    input.payload[0] = (uint8_t)route;
    if (fi_frame_encode(&input, bytes, sizeof(bytes), &length) != FI_OK) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (corrupt_crc) {
        bytes[FI_FIXED_FIELDS_SIZE] ^= 0x01u;
    }

    for (i = 0u; i < length; ++i) {
        const fi_stream_result_t stream_result = fi_stream_decoder_feed(
            decoder, bytes[i], &decoded, &decode_status);
        if (stream_result == FI_STREAM_FRAME_DROPPED) {
            report->rejected_frames += 1u;
            return FI_CONTROL_ERR_ARGUMENT;
        }
        if (stream_result == FI_STREAM_FRAME_READY) {
            fi_control_result_t control_result = fi_controller_apply_result(
                controller,
                decoded.session_id,
                decoded.job_id,
                decoded.seq,
                (fi_route_t)decoded.payload[0],
                now_ms);
            if (control_result != FI_CONTROL_OK) {
                report->rejected_commands += 1u;
            }
            return control_result;
        }
    }
    return FI_CONTROL_ERR_ARGUMENT;
}

static void finish_report(
    fi_sim_report_t *report,
    const fi_controller_t *controller,
    fi_route_t selected_route)
{
    report->final_state = controller->state;
    report->fault = controller->fault;
    report->selected_route = selected_route;
    report->completed_jobs = controller->completed_jobs;
}

static int simulate_completed_route(fi_route_t route, fi_sim_report_t *report)
{
    fi_controller_t controller;
    fi_stream_decoder_t decoder;

    if (report == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    fi_stream_decoder_init(&decoder);
    if (prepare_job(&controller, 1u) != 0) {
        return 1;
    }
    if (deliver_result(&controller, &decoder, 1u, 1u, route, 500u, 0, report)
        != FI_CONTROL_OK) {
        return 1;
    }
    if (fi_controller_route_ready(&controller, 1u) != FI_CONTROL_OK
        || fi_controller_release_started(&controller, 1u) != FI_CONTROL_OK
        || fi_controller_confirm_exit(&controller, SIM_SESSION, 1u, route, 900u)
            != FI_CONTROL_OK) {
        return 1;
    }
    finish_report(report, &controller, route);
    return 0;
}

int fi_simulate_normal(fi_sim_report_t *report)
{
    return simulate_completed_route(FI_ROUTE_PASS, report);
}

int fi_simulate_review(fi_sim_report_t *report)
{
    return simulate_completed_route(FI_ROUTE_REVIEW, report);
}

int fi_simulate_bad_crc(fi_sim_report_t *report)
{
    fi_controller_t controller;
    fi_stream_decoder_t decoder;

    if (report == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    fi_stream_decoder_init(&decoder);
    if (prepare_job(&controller, 1u) != 0) {
        return 1;
    }
    (void)deliver_result(&controller, &decoder, 1u, 1u, FI_ROUTE_PASS, 500u, 1, report);
    finish_report(report, &controller, FI_ROUTE_NONE);
    return report->rejected_frames == 1u ? 0 : 1;
}

int fi_simulate_duplicate(fi_sim_report_t *report)
{
    fi_controller_t controller;
    fi_stream_decoder_t decoder;

    if (report == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    fi_stream_decoder_init(&decoder);
    if (prepare_job(&controller, 1u) != 0
        || deliver_result(&controller, &decoder, 1u, 1u, FI_ROUTE_PASS, 500u, 0, report)
            != FI_CONTROL_OK
        || fi_controller_route_ready(&controller, 1u) != FI_CONTROL_OK
        || fi_controller_release_started(&controller, 1u) != FI_CONTROL_OK
        || fi_controller_confirm_exit(&controller, SIM_SESSION, 1u, FI_ROUTE_PASS, 900u)
            != FI_CONTROL_OK) {
        return 1;
    }
    if (fi_controller_arrive(&controller, SIM_SESSION, 2u, 1000u) != FI_CONTROL_OK
        || fi_controller_begin_stabilization(&controller) != FI_CONTROL_OK
        || fi_controller_request_capture(&controller) != FI_CONTROL_OK) {
        return 1;
    }
    (void)deliver_result(&controller, &decoder, 2u, 1u, FI_ROUTE_PASS, 1200u, 0, report);
    finish_report(report, &controller, FI_ROUTE_PASS);
    return report->rejected_commands == 1u ? 0 : 1;
}

int fi_simulate_timeout(fi_sim_report_t *report)
{
    fi_controller_t controller;

    if (report == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    if (prepare_job(&controller, 1u) != 0
        || fi_controller_tick(&controller, 1101u) != FI_CONTROL_EXPIRED) {
        return 1;
    }
    finish_report(report, &controller, FI_ROUTE_NONE);
    return 0;
}

#ifdef FI_SIMULATOR_MAIN
typedef int (*scenario_fn_t)(fi_sim_report_t *report);

typedef struct {
    const char *name;
    scenario_fn_t run;
} scenario_t;

static const char *state_name(fi_controller_state_t state)
{
    switch (state) {
    case FI_STATE_IDLE: return "IDLE";
    case FI_STATE_ARRIVED: return "ARRIVED";
    case FI_STATE_WAIT_STABLE: return "WAIT_STABLE";
    case FI_STATE_WAIT_RESULT: return "WAIT_RESULT";
    case FI_STATE_SET_ROUTE: return "SET_ROUTE";
    case FI_STATE_RELEASE: return "RELEASE";
    case FI_STATE_VERIFY: return "VERIFY";
    case FI_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

int main(void)
{
    static const scenario_t scenarios[] = {
        {"normal-pass", fi_simulate_normal},
        {"low-confidence-review", fi_simulate_review},
        {"bad-crc", fi_simulate_bad_crc},
        {"duplicate-sequence", fi_simulate_duplicate},
        {"authorization-timeout", fi_simulate_timeout}
    };
    size_t i;
    int failed = 0;

    for (i = 0u; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        fi_sim_report_t report;
        const int result = scenarios[i].run(&report);
        printf(
            "%s result=%s state=%s completed=%lu rejected_frames=%lu rejected_commands=%lu fault=%d\n",
            scenarios[i].name,
            result == 0 ? "PASS" : "FAIL",
            state_name(report.final_state),
            (unsigned long)report.completed_jobs,
            (unsigned long)report.rejected_frames,
            (unsigned long)report.rejected_commands,
            (int)report.fault);
        if (result != 0) {
            failed = 1;
        }
    }
    return failed;
}
#endif

