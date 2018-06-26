/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Authors: Robert Balas (balasr@student.ethz.ch)
 */
#ifndef __RT_DATA_TRACE_DEBUGGER_H__
#define __RT_DATA_TRACE_DEBUGGER_H__
typedef struct {
    unsigned int buffer_size;
    unsigned int dummy;
} rt_trace_dbg_conf_t;

typedef struct {
    /* TODO: not entirely clear what goes into this one */
    int channel;
    unsigned int buffer_size;
    // uint8_t *trace_buffs[2];
    // rt_event_sched_t *sched_i;
} rt_trace_dbg_t;
#endif