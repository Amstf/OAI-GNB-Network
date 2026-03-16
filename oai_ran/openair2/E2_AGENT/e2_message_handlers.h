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

#ifndef OPENAIRINTERFACE_E2_MESSAGE_HANDLERS_H
#define OPENAIRINTERFACE_E2_MESSAGE_HANDLERS_H

#include "oai-oran-protolib/builds/ran_messages.pb-c.h"

#include <netinet/in.h>
#include <stdbool.h>

struct gNB_MAC_INST_s;

void handle_master_message(void *buf, int buflen, int out_socket, struct sockaddr_in peeraddr);
void handle_subscription(RANMessage *message);
void handle_indication_request(RANMessage *message, int out_socket, struct sockaddr_in peeraddr);
void build_indication_response(RANMessage *message, int out_socket, struct sockaddr_in peeraddr);
void apply_slicing_ctrl(SlicingControlM *slicing_ctrl);
void apply_max_cell_prb(int max_prb);
void apply_true_gbr(int true_gbr);
void apply_ue_info(UeListM *ue_list);
UeListM *get_ue_list(void);
void free_ue_list(UeListM *ue_list);
void ran_write(RANParamMapEntry *target_param_map_entry);
void ran_read(RANParameter parameter, RANParamMapEntry *entry);

#endif /* OPENAIRINTERFACE_E2_MESSAGE_HANDLERS_H */
