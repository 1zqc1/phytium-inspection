#ifndef FTINSPECT_CONTROLLER_H
#define FTINSPECT_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FI_STATE_IDLE = 0,
    FI_STATE_ARRIVED,
    FI_STATE_WAIT_STABLE,
    FI_STATE_WAIT_RESULT,
    FI_STATE_SET_ROUTE,
    FI_STATE_RELEASE,
    FI_STATE_VERIFY,
    FI_STATE_FAULT
} fi_controller_state_t;

typedef enum {
    FI_ROUTE_NONE = 0,
    FI_ROUTE_PASS = 1,
    FI_ROUTE_REVIEW = 2
} fi_route_t;

typedef enum {
    FI_FAULT_NONE = 0,
    FI_FAULT_AUTH_TIMEOUT,
    FI_FAULT_ACTION_TIMEOUT,
    FI_FAULT_WRONG_EXIT
} fi_fault_t;

typedef enum {
    FI_CONTROL_OK = 0,
    FI_CONTROL_ERR_ARGUMENT,
    FI_CONTROL_INVALID_STATE,
    FI_CONTROL_SESSION_MISMATCH,
    FI_CONTROL_JOB_MISMATCH,
    FI_CONTROL_SEQUENCE_REJECTED,
    FI_CONTROL_EXPIRED,
    FI_CONTROL_ROUTE_MISMATCH
} fi_control_result_t;

typedef struct {
    fi_controller_state_t state;
    fi_fault_t fault;
    fi_route_t route;
    uint32_t session_id;
    uint32_t job_id;
    uint32_t last_seq;
    uint32_t arrival_ms;
    uint32_t auth_deadline_ms;
    uint32_t action_deadline_ms;
    uint32_t auth_timeout_ms;
    uint32_t action_timeout_ms;
    uint32_t completed_jobs;
} fi_controller_t;

void fi_controller_reset(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t auth_timeout_ms,
    uint32_t action_timeout_ms);
fi_control_result_t fi_controller_arrive(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    uint32_t now_ms);
fi_control_result_t fi_controller_begin_stabilization(fi_controller_t *controller);
fi_control_result_t fi_controller_request_capture(fi_controller_t *controller);
fi_control_result_t fi_controller_apply_result(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    uint32_t seq,
    fi_route_t route,
    uint32_t now_ms);
fi_control_result_t fi_controller_route_ready(fi_controller_t *controller, uint32_t job_id);
fi_control_result_t fi_controller_release_started(fi_controller_t *controller, uint32_t job_id);
fi_control_result_t fi_controller_confirm_exit(
    fi_controller_t *controller,
    uint32_t session_id,
    uint32_t job_id,
    fi_route_t observed_route,
    uint32_t now_ms);
fi_control_result_t fi_controller_tick(fi_controller_t *controller, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

