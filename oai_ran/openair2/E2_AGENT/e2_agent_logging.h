/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#pragma once

#include "common/utils/LOG/log.h"

/*
 * When the T tracer has not generated dedicated legacy IDs for the E2 agent
 * (for example in builds where the agent is compiled but the tracer database
 * has not been regenerated yet), fall back to an existing component so that
 * the LOG_* macros still compile.
 */
#ifdef T_LEGACY_E2_AGENT_INFO
#define E2_LOG_E(...) LOG_E(E2_AGENT, __VA_ARGS__)
#define E2_LOG_W(...) LOG_W(E2_AGENT, __VA_ARGS__)
#define E2_LOG_I(...) LOG_I(E2_AGENT, __VA_ARGS__)
#define E2_LOG_D(...) LOG_D(E2_AGENT, __VA_ARGS__)
#define E2_LOG_DUMP(...) LOG_DDUMP(E2_AGENT, __VA_ARGS__)
#else
#define E2_LOG_E(...) LOG_E(GNB_APP, __VA_ARGS__)
#define E2_LOG_W(...) LOG_W(GNB_APP, __VA_ARGS__)
#define E2_LOG_I(...) LOG_I(GNB_APP, __VA_ARGS__)
#define E2_LOG_D(...) LOG_D(GNB_APP, __VA_ARGS__)
#define E2_LOG_DUMP(...) LOG_DDUMP(GNB_APP, __VA_ARGS__)
#endif
