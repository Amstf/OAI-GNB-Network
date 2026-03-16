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

#include "e2_agent_app.h"
#include "e2_message_handlers.h"
#include "e2_agent_logging.h"
#include "common/utils/system.h"
#include "intertask_interface.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static pthread_t heartbeat_thread;
e2_agent_databank_t *e2_agent_db = NULL;

static int setup_socket(int port)
{
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    E2_LOG_E("Failed to create UDP socket: %s\n", strerror(errno));
    return -1;
  }

  int reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    E2_LOG_W("Unable to enable SO_REUSEADDR: %s\n", strerror(errno));

  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(port)};
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    E2_LOG_E("Failed to bind UDP socket on port %d: %s\n", port, strerror(errno));
    close(sock);
    return -1;
  }

  return sock;
}

int e2_agent_init(void)
{
  e2_agent_info_t *agent = calloc(1, sizeof(*agent));
  AssertFatal(agent != NULL, "Out of memory while allocating E2 agent context\n");

  int rc = pthread_mutex_init(&agent->heartbeat_mutex, NULL);
  AssertFatal(rc == 0, "Unable to initialise heartbeat mutex (%d)\n", rc);

  agent->in_socket = setup_socket(E2AGENT_IN_PORT);
  AssertFatal(agent->in_socket >= 0, "Failed to open inbound E2 agent socket\n");

  agent->out_socket = socket(AF_INET, SOCK_DGRAM, 0);
  AssertFatal(agent->out_socket >= 0, "Failed to open outbound E2 agent socket: %s\n", strerror(errno));

  agent->peer_addr.sin_family = AF_INET;
  agent->peer_addr.sin_port = htons(E2AGENT_OUT_PORT);
  agent->peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  e2_agent_db = calloc(1, sizeof(*e2_agent_db));
  AssertFatal(e2_agent_db != NULL, "Out of memory while creating agent data bank\n");

  rc = pthread_mutex_init(&e2_agent_db->mutex, NULL);
  AssertFatal(rc == 0, "Unable to initialise agent data bank mutex (%d)\n", rc);
  e2_agent_db->max_prb = -1;
  e2_agent_db->true_gbr = 0;

  rc = pthread_create(&heartbeat_thread, NULL, e2_heartbeat, agent);
  AssertFatal(rc == 0, "Failed to start E2 heartbeat thread (%d)\n", rc);
  pthread_detach(heartbeat_thread);

  ittiTask_parms_t task_params = {.args_to_start_routine = agent, .shortcut_func = NULL};
  rc = itti_create_task(TASK_E2_AGENT, e2_agent_task, &task_params);
  AssertFatal(rc >= 0, "cannot create ITTI E2 agent task\n");

  E2_LOG_I("E2 agent initialised (ports %d/%d)\n", E2AGENT_IN_PORT, E2AGENT_OUT_PORT);
  return 0;
}

void *e2_agent_task(void *args)
{
  e2_agent_info_t *info = (e2_agent_info_t *)args;
  itti_mark_task_ready(TASK_E2_AGENT);

  uint8_t buffer[E2AGENT_MAX_BUF_SIZE];
  socklen_t addr_len = sizeof(info->listen_addr);

  while (1) {
    ssize_t bytes = recvfrom(info->in_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&info->listen_addr, &addr_len);
    if (bytes < 0) {
      if (errno == EINTR)
        continue;
      E2_LOG_E("recvfrom failed: %s\n", strerror(errno));
      continue;
    }

    E2_LOG_D("Received %zd bytes from %s:%d\n", bytes, inet_ntoa(info->listen_addr.sin_addr), ntohs(info->listen_addr.sin_port));
    handle_master_message(buffer, (int)bytes, info->out_socket, info->peer_addr);
  }

  return NULL;
}

void *e2_heartbeat(void *args)
{
  e2_agent_info_t *info = (e2_agent_info_t *)args;
  while (1) {
    pthread_mutex_lock(&info->heartbeat_mutex);
    E2_LOG_I("E2 agent heartbeat\n");
    pthread_mutex_unlock(&info->heartbeat_mutex);
    sleep(3);
  }

  return NULL;
}
