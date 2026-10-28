#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ftinspect/protocol.h"
#include "ftinspect/stream_decoder.h"
#include "ftinspect/controller.h"
#include "simulator.h"

static int failures = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            failures += 1;                                                                  \
            return;                                                                         \
        }                                                                                   \
    } while (0)

static fi_frame_t sample_frame(void)
{
    fi_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = FI_MSG_RESULT;
    frame.session_id = 0x11223344u;
    frame.job_id = 0x55667788u;
    frame.seq = 0xAABBCCDDu;
    frame.payload_length = 3u;
    frame.payload[0] = 0x02u;
    frame.payload[1] = 0x34u;
    frame.payload[2] = 0x12u;
    return frame;
}

static void test_crc_known_vector(void)
{
    static const uint8_t input[] = "123456789";
    CHECK(fi_crc16_ccitt(input, 9u) == 0x29B1u);
}

static void test_frame_encode_matches_wire_fixture(void)
{
    static const uint8_t expected[] = {
        0xA5u, 0x5Au, 0x01u, 0x12u, 0x03u, 0x00u,
        0x44u, 0x33u, 0x22u, 0x11u,
        0x88u, 0x77u, 0x66u, 0x55u,
        0xDDu, 0xCCu, 0xBBu, 0xAAu,
        0x02u, 0x34u, 0x12u, 0x8Eu, 0xA8u
    };
    fi_frame_t frame = sample_frame();
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t written = 0u;

    CHECK(fi_frame_encode(&frame, bytes, sizeof(bytes), &written) == FI_OK);
    CHECK(written == sizeof(expected));
    CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
}

static void test_frame_decode_round_trip(void)
{
    fi_frame_t input = sample_frame();
    fi_frame_t output;
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t written = 0u;

    CHECK(fi_frame_encode(&input, bytes, sizeof(bytes), &written) == FI_OK);
    CHECK(fi_frame_decode(bytes, written, &output) == FI_OK);
    CHECK(output.type == input.type);
    CHECK(output.session_id == input.session_id);
    CHECK(output.job_id == input.job_id);
    CHECK(output.seq == input.seq);
    CHECK(output.payload_length == input.payload_length);
    CHECK(memcmp(output.payload, input.payload, input.payload_length) == 0);
}

static void test_frame_decode_rejects_corruption(void)
{
    fi_frame_t frame = sample_frame();
    fi_frame_t output;
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t written = 0u;

    CHECK(fi_frame_encode(&frame, bytes, sizeof(bytes), &written) == FI_OK);

    bytes[0] ^= 0x01u;
    CHECK(fi_frame_decode(bytes, written, &output) == FI_ERR_MAGIC);
    bytes[0] ^= 0x01u;

    bytes[2] = 0x02u;
    CHECK(fi_frame_decode(bytes, written, &output) == FI_ERR_VERSION);
    bytes[2] = FI_PROTOCOL_VERSION;

    CHECK(fi_frame_decode(bytes, written - 1u, &output) == FI_ERR_LENGTH);

    bytes[18] ^= 0x01u;
    CHECK(fi_frame_decode(bytes, written, &output) == FI_ERR_CRC);
    bytes[18] ^= 0x01u;

    bytes[3] = 0x7Fu;
    CHECK(fi_frame_decode(bytes, written, &output) == FI_ERR_TYPE);
}

static void test_frame_encode_rejects_oversized_payload(void)
{
    fi_frame_t frame = sample_frame();
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t written = 0u;
    frame.payload_length = FI_MAX_PAYLOAD_SIZE + 1u;

    CHECK(fi_frame_encode(&frame, bytes, sizeof(bytes), &written) == FI_ERR_LENGTH);
    CHECK(written == 0u);
}

static void test_stream_decoder_accepts_fragmented_frame(void)
{
    fi_frame_t input = sample_frame();
    fi_frame_t output;
    fi_stream_decoder_t decoder;
    fi_status_t decode_status = FI_OK;
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t written = 0u;
    size_t i;

    CHECK(fi_frame_encode(&input, bytes, sizeof(bytes), &written) == FI_OK);
    fi_stream_decoder_init(&decoder);
    for (i = 0u; i + 1u < written; ++i) {
        CHECK(fi_stream_decoder_feed(&decoder, bytes[i], &output, &decode_status)
            == FI_STREAM_NEED_MORE);
    }
    CHECK(fi_stream_decoder_feed(&decoder, bytes[written - 1u], &output, &decode_status)
        == FI_STREAM_FRAME_READY);
    CHECK(decode_status == FI_OK);
    CHECK(output.job_id == input.job_id);
    CHECK(output.seq == input.seq);
    CHECK(output.payload_length == input.payload_length);
    CHECK(memcmp(output.payload, input.payload, input.payload_length) == 0);
}

static void test_stream_decoder_recovers_after_noise_and_bad_frame(void)
{
    static const uint8_t noise[] = {0x00u, 0xA5u, 0x11u, 0x5Au, 0xFEu};
    fi_frame_t first = sample_frame();
    fi_frame_t second = sample_frame();
    fi_frame_t output;
    fi_stream_decoder_t decoder;
    fi_status_t decode_status = FI_OK;
    uint8_t valid_first[FI_MAX_FRAME_SIZE];
    uint8_t valid_second[FI_MAX_FRAME_SIZE];
    uint8_t bad[FI_MAX_FRAME_SIZE];
    size_t first_length = 0u;
    size_t second_length = 0u;
    size_t i;
    unsigned int ready_count = 0u;
    unsigned int dropped_count = 0u;

    first.job_id = 10u;
    first.seq = 1u;
    second.job_id = 11u;
    second.seq = 2u;
    CHECK(fi_frame_encode(&first, valid_first, sizeof(valid_first), &first_length) == FI_OK);
    CHECK(fi_frame_encode(&second, valid_second, sizeof(valid_second), &second_length) == FI_OK);
    memcpy(bad, valid_first, first_length);
    bad[18] ^= 0x40u;

    fi_stream_decoder_init(&decoder);
    for (i = 0u; i < sizeof(noise); ++i) {
        CHECK(fi_stream_decoder_feed(&decoder, noise[i], &output, &decode_status)
            == FI_STREAM_NEED_MORE);
    }
    for (i = 0u; i < first_length; ++i) {
        fi_stream_result_t result = fi_stream_decoder_feed(
            &decoder, bad[i], &output, &decode_status);
        if (result == FI_STREAM_FRAME_DROPPED) {
            dropped_count += 1u;
            CHECK(decode_status == FI_ERR_CRC);
        }
    }
    for (i = 0u; i < first_length; ++i) {
        if (fi_stream_decoder_feed(&decoder, valid_first[i], &output, &decode_status)
            == FI_STREAM_FRAME_READY) {
            ready_count += 1u;
            CHECK(output.job_id == 10u);
        }
    }
    for (i = 0u; i < second_length; ++i) {
        if (fi_stream_decoder_feed(&decoder, valid_second[i], &output, &decode_status)
            == FI_STREAM_FRAME_READY) {
            ready_count += 1u;
            CHECK(output.job_id == 11u);
        }
    }
    CHECK(dropped_count == 1u);
    CHECK(ready_count == 2u);
}

static void test_stream_timeout_preserves_new_frame_start(void)
{
    fi_frame_t input = sample_frame(), output;
    fi_stream_decoder_t decoder;
    fi_status_t status = FI_OK;
    uint8_t bytes[FI_MAX_FRAME_SIZE];
    size_t length = 0u, i;
    CHECK(fi_frame_encode(&input, bytes, sizeof(bytes), &length) == FI_OK);
    fi_stream_decoder_init(&decoder);
    CHECK(fi_stream_decoder_feed_timed(&decoder, 0xA5u, &output, &status, 0u, 10u) == FI_STREAM_NEED_MORE);
    CHECK(fi_stream_decoder_feed_timed(&decoder, 0x5Au, &output, &status, 1u, 10u) == FI_STREAM_NEED_MORE);
    CHECK(fi_stream_decoder_feed_timed(&decoder, bytes[0], &output, &status, 11u, 10u) == FI_STREAM_FRAME_DROPPED);
    CHECK(status == FI_ERR_TIMEOUT);
    for (i = 1u; i + 1u < length; ++i) {
        CHECK(fi_stream_decoder_feed_timed(&decoder, bytes[i], &output, &status, 11u, 10u) == FI_STREAM_NEED_MORE);
    }
    CHECK(fi_stream_decoder_feed_timed(&decoder, bytes[length - 1u], &output, &status, 11u, 10u) == FI_STREAM_FRAME_READY);
    CHECK(output.job_id == input.job_id);
}

static void drive_to_wait_result(fi_controller_t *controller, uint32_t session, uint32_t job)
{
    CHECK(fi_controller_heartbeat(controller, session, 100u) == FI_CONTROL_OK);
    CHECK(fi_controller_arrive(controller, session, job, 100u) == FI_CONTROL_OK);
    CHECK(fi_controller_begin_stabilization(controller) == FI_CONTROL_OK);
    CHECK(fi_controller_request_capture(controller) == FI_CONTROL_OK);
    CHECK(controller->state == FI_STATE_WAIT_RESULT);
}

static void heartbeat_until(fi_controller_t *controller, uint32_t now_ms)
{
    while ((uint32_t)(now_ms - controller->last_heartbeat_ms) >= 100u) {
        CHECK(fi_controller_heartbeat(controller, controller->session_id,
            controller->last_heartbeat_ms + 100u) == FI_CONTROL_OK);
    }
}

static void test_controller_completes_one_normal_job(void)
{
    fi_controller_t controller;

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    CHECK(controller.state == FI_STATE_IDLE);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    CHECK(controller.state == FI_STATE_SET_ROUTE);
    heartbeat_until(&controller, 600u);
    CHECK(fi_controller_route_ready(&controller, 7u, 600u) == FI_CONTROL_OK);
    CHECK(controller.state == FI_STATE_RELEASE);
    CHECK(fi_controller_release_started(&controller, 7u, 700u) == FI_CONTROL_OK);
    CHECK(controller.state == FI_STATE_VERIFY);
    heartbeat_until(&controller, 900u);
    CHECK(fi_controller_confirm_exit(
        &controller, 0x1001u, 7u, FI_ROUTE_PASS, 900u) == FI_CONTROL_OK);
    CHECK(controller.state == FI_STATE_IDLE);
    CHECK(controller.completed_jobs == 1u);
    CHECK(controller.fault == FI_FAULT_NONE);
}

static void test_controller_rejects_duplicate_and_wrong_job(void)
{
    fi_controller_t controller;

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 400u);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 8u, 1u, FI_ROUTE_PASS, 400u) == FI_CONTROL_JOB_MISMATCH);
    CHECK(controller.state == FI_STATE_WAIT_RESULT);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 7u, 1u, FI_ROUTE_REVIEW, 400u) == FI_CONTROL_OK);
    CHECK(controller.route == FI_ROUTE_REVIEW);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_route_ready(&controller, 7u, 500u) == FI_CONTROL_OK);
    CHECK(fi_controller_release_started(&controller, 7u, 600u) == FI_CONTROL_OK);
    heartbeat_until(&controller, 700u);
    CHECK(fi_controller_confirm_exit(
        &controller, 0x1001u, 7u, FI_ROUTE_REVIEW, 700u) == FI_CONTROL_OK);

    CHECK(fi_controller_heartbeat(&controller, 0x1001u, 800u) == FI_CONTROL_OK);
    CHECK(fi_controller_arrive(&controller, 0x1001u, 8u, 800u) == FI_CONTROL_OK);
    CHECK(fi_controller_begin_stabilization(&controller) == FI_CONTROL_OK);
    CHECK(fi_controller_request_capture(&controller) == FI_CONTROL_OK);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 8u, 1u, FI_ROUTE_PASS, 800u) == FI_CONTROL_SEQUENCE_REJECTED);
    CHECK(controller.state == FI_STATE_WAIT_RESULT);
    CHECK(controller.completed_jobs == 1u);
}

static void test_controller_enforces_local_deadlines(void)
{
    fi_controller_t controller;

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    CHECK(fi_controller_tick(&controller, 1101u) == FI_CONTROL_EXPIRED);
    CHECK(controller.state == FI_STATE_FAULT);
    CHECK(controller.fault == FI_FAULT_AUTH_TIMEOUT);

    fi_controller_reset(&controller, 0x1002u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1002u, 8u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(
        &controller, 0x1002u, 8u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    CHECK(fi_controller_tick(&controller, 2501u) == FI_CONTROL_EXPIRED);
    CHECK(controller.state == FI_STATE_FAULT);
    CHECK(controller.fault == FI_FAULT_ACTION_TIMEOUT);
}

static void test_heartbeat_gates_actions(void)
{
    fi_controller_t controller;
    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    CHECK(fi_controller_arrive(&controller, 0x1001u, 7u, 100u) == FI_CONTROL_HEARTBEAT_LOST);
    CHECK(fi_controller_heartbeat(&controller, 0x1002u, 100u) == FI_CONTROL_SESSION_MISMATCH);
    CHECK(fi_controller_heartbeat(&controller, 0x1001u, 100u) == FI_CONTROL_OK);
    CHECK(fi_controller_arrive(&controller, 0x1001u, 7u, 399u) == FI_CONTROL_OK);
    CHECK(fi_controller_tick(&controller, 400u) == FI_CONTROL_HEARTBEAT_LOST);
    CHECK(controller.state == FI_STATE_FAULT);
    CHECK(controller.fault == FI_FAULT_HEARTBEAT_TIMEOUT);
    CHECK(fi_controller_heartbeat(&controller, 0x1001u, 401u) == FI_CONTROL_INVALID_STATE);

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    CHECK(fi_controller_apply_result(&controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 400u) == FI_CONTROL_HEARTBEAT_LOST);
    CHECK(controller.fault == FI_FAULT_HEARTBEAT_TIMEOUT);

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    CHECK(fi_controller_heartbeat(&controller, 0x1001u, 400u) == FI_CONTROL_HEARTBEAT_LOST);
    CHECK(controller.state == FI_STATE_FAULT);

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(&controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    CHECK(fi_controller_route_ready(&controller, 7u, 800u) == FI_CONTROL_HEARTBEAT_LOST);
    CHECK(controller.state == FI_STATE_FAULT);
}

static void test_deadline_is_exclusive(void)
{
    fi_controller_t controller;
    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 1100u) == FI_CONTROL_EXPIRED);
    CHECK(controller.fault == FI_FAULT_AUTH_TIMEOUT);
}

static void test_timeout_configuration_outside_wrap_safe_range_is_rejected(void)
{
    fi_controller_t controller;
    fi_controller_reset(&controller, 1u, 0x80000000u, 2000u);
    CHECK(controller.state == FI_STATE_FAULT);
    CHECK(fi_controller_arrive(&controller, 1u, 1u, 0u) == FI_CONTROL_INVALID_STATE);
}

static void test_action_transitions_reject_expired_deadline_without_tick(void)
{
    fi_controller_t controller;
    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(&controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    CHECK(fi_controller_route_ready(&controller, 7u, 2500u) == FI_CONTROL_EXPIRED);
    CHECK(controller.fault == FI_FAULT_ACTION_TIMEOUT);
    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(&controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    heartbeat_until(&controller, 2499u);
    CHECK(fi_controller_route_ready(&controller, 7u, 2499u) == FI_CONTROL_OK);
    CHECK(fi_controller_release_started(&controller, 7u, 2500u) == FI_CONTROL_EXPIRED);
    CHECK(controller.fault == FI_FAULT_ACTION_TIMEOUT);
}

static void test_controller_faults_on_wrong_exit(void)
{
    fi_controller_t controller;

    fi_controller_reset(&controller, 0x1001u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x1001u, 7u);
    heartbeat_until(&controller, 500u);
    CHECK(fi_controller_apply_result(
        &controller, 0x1001u, 7u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_OK);
    CHECK(fi_controller_route_ready(&controller, 7u, 600u) == FI_CONTROL_OK);
    CHECK(fi_controller_release_started(&controller, 7u, 700u) == FI_CONTROL_OK);
    heartbeat_until(&controller, 900u);
    CHECK(fi_controller_confirm_exit(
        &controller, 0x1001u, 7u, FI_ROUTE_REVIEW, 900u) == FI_CONTROL_ROUTE_MISMATCH);
    CHECK(controller.state == FI_STATE_FAULT);
    CHECK(controller.fault == FI_FAULT_WRONG_EXIT);
    CHECK(controller.completed_jobs == 0u);
}

static void test_controller_rejects_previous_session_after_reset(void)
{
    fi_controller_t controller;

    fi_controller_reset(&controller, 0x2002u, 1000u, 2000u);
    drive_to_wait_result(&controller, 0x2002u, 9u);
    CHECK(fi_controller_apply_result(
        &controller, 0x2001u, 9u, 1u, FI_ROUTE_PASS, 500u) == FI_CONTROL_SESSION_MISMATCH);
    CHECK(controller.state == FI_STATE_WAIT_RESULT);
    CHECK(controller.last_seq == 0u);
}

static void test_simulator_scenarios_have_expected_outcomes(void)
{
    fi_sim_report_t report;

    CHECK(fi_simulate_normal(&report) == 0);
    CHECK(report.final_state == FI_STATE_IDLE);
    CHECK(report.completed_jobs == 1u);
    CHECK(report.selected_route == FI_ROUTE_PASS);

    CHECK(fi_simulate_review(&report) == 0);
    CHECK(report.final_state == FI_STATE_IDLE);
    CHECK(report.completed_jobs == 1u);
    CHECK(report.selected_route == FI_ROUTE_REVIEW);

    CHECK(fi_simulate_bad_crc(&report) == 0);
    CHECK(report.final_state == FI_STATE_WAIT_RESULT);
    CHECK(report.rejected_frames == 1u);
    CHECK(report.completed_jobs == 0u);

    CHECK(fi_simulate_duplicate(&report) == 0);
    CHECK(report.final_state == FI_STATE_WAIT_RESULT);
    CHECK(report.rejected_commands == 1u);
    CHECK(report.completed_jobs == 1u);

    CHECK(fi_simulate_timeout(&report) == 0);
    CHECK(report.final_state == FI_STATE_FAULT);
    CHECK(report.fault == FI_FAULT_AUTH_TIMEOUT);
}

typedef void (*test_fn_t)(void);

static void run_test(const char *name, test_fn_t test)
{
    const int before = failures;
    test();
    if (failures == before) {
        printf("PASS %s\n", name);
    }
}

int main(void)
{
    run_test("crc known vector", test_crc_known_vector);
    run_test("frame encode fixture", test_frame_encode_matches_wire_fixture);
    run_test("frame decode round trip", test_frame_decode_round_trip);
    run_test("frame corruption rejection", test_frame_decode_rejects_corruption);
    run_test("oversized payload rejection", test_frame_encode_rejects_oversized_payload);
    run_test("fragmented stream frame", test_stream_decoder_accepts_fragmented_frame);
    run_test("stream resynchronization", test_stream_decoder_recovers_after_noise_and_bad_frame);
    run_test("stream timeout recovery", test_stream_timeout_preserves_new_frame_start);
    run_test("controller normal job", test_controller_completes_one_normal_job);
    run_test("controller duplicate and job rejection", test_controller_rejects_duplicate_and_wrong_job);
    run_test("controller local deadlines", test_controller_enforces_local_deadlines);
    run_test("heartbeat gates actions", test_heartbeat_gates_actions);
    run_test("deadline is exclusive", test_deadline_is_exclusive);
    run_test("invalid timeout is rejected", test_timeout_configuration_outside_wrap_safe_range_is_rejected);
    run_test("expired transitions without tick", test_action_transitions_reject_expired_deadline_without_tick);
    run_test("controller wrong exit fault", test_controller_faults_on_wrong_exit);
    run_test("controller session reset", test_controller_rejects_previous_session_after_reset);
    run_test("simulator scenarios", test_simulator_scenarios_have_expected_outcomes);
    return failures == 0 ? 0 : 1;
}
