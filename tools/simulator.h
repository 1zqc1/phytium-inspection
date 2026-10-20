#ifndef FTINSPECT_SIMULATOR_H
#define FTINSPECT_SIMULATOR_H

#include <stdint.h>

#include "ftinspect/controller.h"

typedef struct {
    fi_controller_state_t final_state;
    fi_fault_t fault;
    fi_route_t selected_route;
    uint32_t completed_jobs;
    uint32_t rejected_frames;
    uint32_t rejected_commands;
} fi_sim_report_t;

int fi_simulate_normal(fi_sim_report_t *report);
int fi_simulate_review(fi_sim_report_t *report);
int fi_simulate_bad_crc(fi_sim_report_t *report);
int fi_simulate_duplicate(fi_sim_report_t *report);
int fi_simulate_timeout(fi_sim_report_t *report);

#endif

