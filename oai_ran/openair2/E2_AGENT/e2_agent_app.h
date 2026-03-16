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

#ifndef OPENAIRINTERFACE_E2_AGENT_APP_H
#define OPENAIRINTERFACE_E2_AGENT_APP_H

#include "oai-oran-protolib/builds/ran_messages.pb-c.h"

#include <netinet/in.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define E2AGENT_IN_PORT 6655
#define E2AGENT_OUT_PORT 6600
#define E2AGENT_MAX_BUF_SIZE 2048

typedef struct e2_agent_info_s {
  int in_socket;
  int out_socket;
  int reuse;
  struct sockaddr_in listen_addr;
  struct sockaddr_in peer_addr;
  pthread_mutex_t heartbeat_mutex;
} e2_agent_info_t;

typedef struct e2_agent_databank_s {
  pthread_mutex_t mutex;
  int max_prb;
  int true_gbr;
} e2_agent_databank_t;

extern e2_agent_databank_t *e2_agent_db;

int e2_agent_init(void);
void *e2_agent_task(void *args);
void *e2_heartbeat(void *args);

void handle_master_message(void *buffer, int length, int out_socket, struct sockaddr_in peer_addr);

#ifdef __cplusplus
}
#endif

#endif /* OPENAIRINTERFACE_E2_AGENT_APP_H */
