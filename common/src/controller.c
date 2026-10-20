#include "ftinspect/controller.h"

#include <stddef.h>
#include <string.h>

static int deadline_passed(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) > 0;
}

static fi_control_result_t enter_fault(fi_controller_t *controller, fi_fault_t fault)
{
    controller->state = FI_STATE_FAULT;
    controller->fault = fault;
    return FI_CONTROL_EXPIRED;
}

static fi_control_result_t check_identity(
    const fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id)
{
    if (session_id != controller->session_id) {
        return FI_CONTROL_SESSION_MISMATCH;
    }
    if (job_id != controller->job_id) {
        return FI_CONTROL_JOB_MISMATCH;
    }
    return FI_CONTROL_OK;
}

void fi_controller_reset(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t auth_timeout_ms,
    uint32_t action_timeout_ms)
{
    if (controller == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->state = FI_STATE_IDLE;
    controller->fault = FI_FAULT_NONE;
    controller->route = FI_ROUTE_NONE;
    controller->session_id = session_id;
    controller->auth_timeout_ms = auth_timeout_ms;
    controller->action_timeout_ms = action_timeout_ms;
}

fi_control_result_t fi_controller_arrive(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    uint32_t now_ms)
{
    if (controller == NULL || job_id == 0u) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_IDLE) {
        return FI_CONTROL_INVALID_STATE;
    }
    if (session_id != controller->session_id) {
        return FI_CONTROL_SESSION_MISMATCH;
    }

    controller->job_id = job_id;
    controller->arrival_ms = now_ms;
    controller->auth_deadline_ms = now_ms + controller->auth_timeout_ms;
    controller->action_deadline_ms = 0u;
    controller->route = FI_ROUTE_NONE;
    controller->fault = FI_FAULT_NONE;
    controller->state = FI_STATE_ARRIVED;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_begin_stabilization(fi_controller_t *controller)
{
    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_ARRIVED) {
        return FI_CONTROL_INVALID_STATE;
    }
    controller->state = FI_STATE_WAIT_STABLE;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_request_capture(fi_controller_t *controller)
{
    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_WAIT_STABLE) {
        return FI_CONTROL_INVALID_STATE;
    }
    controller->state = FI_STATE_WAIT_RESULT;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_apply_result(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    uint32_t seq,
    fi_route_t route,
    uint32_t now_ms)
{
    fi_control_result_t identity;

    if (controller == NULL || (route != FI_ROUTE_PASS && route != FI_ROUTE_REVIEW)) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_WAIT_RESULT) {
        return FI_CONTROL_INVALID_STATE;
    }
    identity = check_identity(controller, session_id, job_id);
    if (identity != FI_CONTROL_OK) {
        return identity;
    }
    if (deadline_passed(now_ms, controller->auth_deadline_ms)) {
        return enter_fault(controller, FI_FAULT_AUTH_TIMEOUT);
    }
    if (seq == 0u || seq <= controller->last_seq) {
        return FI_CONTROL_SEQUENCE_REJECTED;
    }

    controller->last_seq = seq;
    controller->route = route;
    controller->action_deadline_ms = now_ms + controller->action_timeout_ms;
    controller->state = FI_STATE_SET_ROUTE;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_route_ready(fi_controller_t *controller, uint32_t job_id)
{
    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_SET_ROUTE) {
        return FI_CONTROL_INVALID_STATE;
    }
    if (job_id != controller->job_id) {
        return FI_CONTROL_JOB_MISMATCH;
    }
    controller->state = FI_STATE_RELEASE;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_release_started(fi_controller_t *controller, uint32_t job_id)
{
    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_RELEASE) {
        return FI_CONTROL_INVALID_STATE;
    }
    if (job_id != controller->job_id) {
        return FI_CONTROL_JOB_MISMATCH;
    }
    controller->state = FI_STATE_VERIFY;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_confirm_exit(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    fi_route_t observed_route,
    uint32_t now_ms)
{
    fi_control_result_t identity;

    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    if (controller->state != FI_STATE_VERIFY) {
        return FI_CONTROL_INVALID_STATE;
    }
    identity = check_identity(controller, session_id, job_id);
    if (identity != FI_CONTROL_OK) {
        return identity;
    }
    if (deadline_passed(now_ms, controller->action_deadline_ms)) {
        return enter_fault(controller, FI_FAULT_ACTION_TIMEOUT);
    }
    if (observed_route != controller->route) {
        controller->state = FI_STATE_FAULT;
        controller->fault = FI_FAULT_WRONG_EXIT;
        return FI_CONTROL_ROUTE_MISMATCH;
    }

    controller->completed_jobs += 1u;
    controller->state = FI_STATE_IDLE;
    controller->job_id = 0u;
    controller->route = FI_ROUTE_NONE;
    controller->action_deadline_ms = 0u;
    return FI_CONTROL_OK;
}

fi_control_result_t fi_controller_tick(fi_controller_t *controller, uint32_t now_ms)
{
    if (controller == NULL) {
        return FI_CONTROL_ERR_ARGUMENT;
    }
    switch (controller->state) {
    case FI_STATE_ARRIVED:
    case FI_STATE_WAIT_STABLE:
    case FI_STATE_WAIT_RESULT:
        if (deadline_passed(now_ms, controller->auth_deadline_ms)) {
            return enter_fault(controller, FI_FAULT_AUTH_TIMEOUT);
        }
        return FI_CONTROL_OK;
    case FI_STATE_SET_ROUTE:
    case FI_STATE_RELEASE:
    case FI_STATE_VERIFY:
        if (deadline_passed(now_ms, controller->action_deadline_ms)) {
            return enter_fault(controller, FI_FAULT_ACTION_TIMEOUT);
        }
        return FI_CONTROL_OK;
    case FI_STATE_IDLE:
        return FI_CONTROL_OK;
    case FI_STATE_FAULT:
    default:
        return FI_CONTROL_INVALID_STATE;
    }
}
