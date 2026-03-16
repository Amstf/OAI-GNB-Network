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

#include "e2_message_handlers.h"

#include "common/ran_context.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include "e2_agent_app.h"
#include "e2_agent_logging.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

extern RAN_CONTEXT_t RC;

typedef struct {
  rnti_t rnti;
  uint64_t last_dl_rbs;
  uint64_t last_ul_rbs;
  struct timespec last_ts;
  bool initialized;
} ue_prb_history_t;

static ue_prb_history_t prb_history[MAX_MOBILES_PER_GNB] = {0};

static const char *param_name(RANParameter param)
{
  switch (param) {
    case RAN_PARAMETER__GNB_ID:
      return "gnb_id";
    case RAN_PARAMETER__SOMETHING:
      return "something";
    case RAN_PARAMETER__UE_LIST:
      return "ue_list";
    case RAN_PARAMETER__SCHED_INFO_:
      return "sched_info";
    case RAN_PARAMETER__SCHED_CONTROL:
      return "sched_control";
    case RAN_PARAMETER__MAX_PRB:
      return "max_prb";
    case RAN_PARAMETER__USE_TRUE_GBR:
      return "use_true_gbr";
    case RAN_PARAMETER__SLICING_CONTROL:
      return "slicing_control";
    default:
      return "unknown";
  }
}


typedef struct {
  module_id_t mod_id;
  rnti_t rnti;
  uint64_t dl_bytes;
  uint64_t ul_bytes;
  uint32_t dl_rbs;
  uint32_t ul_rbs;
  struct timespec ts;
  bool initialized;
} ue_snapshot_t;

static ue_snapshot_t *get_snapshot(module_id_t mod_id, rnti_t rnti)
{
  static ue_snapshot_t entries[MAX_MOBILES_PER_GNB];
  static bool initialized = false;

  if (!initialized) {
    memset(entries, 0, sizeof(entries));
    initialized = true;
  }

  for (size_t i = 0; i < MAX_MOBILES_PER_GNB; ++i) {
    if (entries[i].initialized && entries[i].mod_id == mod_id && entries[i].rnti == rnti)
      return &entries[i];
  }

  for (size_t i = 0; i < MAX_MOBILES_PER_GNB; ++i) {
    if (!entries[i].initialized) {
      entries[i].initialized = true;
      entries[i].mod_id = mod_id;
      entries[i].rnti = rnti;
      clock_gettime(CLOCK_MONOTONIC, &entries[i].ts);
      return &entries[i];
    }
  }

  return NULL;
}

static void update_avg_tbs_per_prb(module_id_t mod_id,
                                    rnti_t rnti,
                                    const NR_mac_stats_t *stats,
                                    float *avg_tbs_prb_dl,
                                    float *avg_tbs_prb_ul)
{
  if (stats == NULL || avg_tbs_prb_dl == NULL || avg_tbs_prb_ul == NULL)
    return;

  ue_snapshot_t *snap = get_snapshot(mod_id, rnti);
  if (snap == NULL)
    return;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  uint64_t dl_bytes = stats->dl.total_bytes;
  uint64_t ul_bytes = stats->ul.total_bytes;
  uint32_t dl_rbs = stats->dl.total_rbs;
  uint32_t ul_rbs = stats->ul.total_rbs;

  if (snap->dl_rbs > dl_rbs || snap->ul_rbs > ul_rbs)
    snap->initialized = false;

  if (!snap->initialized) {
    snap->dl_bytes = dl_bytes;
    snap->ul_bytes = ul_bytes;
    snap->dl_rbs = dl_rbs;
    snap->ul_rbs = ul_rbs;
    snap->ts = now;
    snap->initialized = true;
    return;
  }

  double dt = (now.tv_sec - snap->ts.tv_sec) + (now.tv_nsec - snap->ts.tv_nsec) / 1e9;
  if (dt <= 0.0)
    dt = 1e-3;

  uint64_t delta_dl_bytes = dl_bytes - snap->dl_bytes;
  uint64_t delta_ul_bytes = ul_bytes - snap->ul_bytes;
  uint32_t delta_dl_rbs = dl_rbs - snap->dl_rbs;
  uint32_t delta_ul_rbs = ul_rbs - snap->ul_rbs;

  if (delta_dl_rbs > 0)
    *avg_tbs_prb_dl = (float)((double)delta_dl_bytes / (double)delta_dl_rbs);
  if (delta_ul_rbs > 0)
    *avg_tbs_prb_ul = (float)((double)delta_ul_bytes / (double)delta_ul_rbs);

  snap->dl_bytes = dl_bytes;
  snap->ul_bytes = ul_bytes;
  snap->dl_rbs = dl_rbs;
  snap->ul_rbs = ul_rbs;
  snap->ts = now;
}

static const char *message_type_name(RANMessageType type)
{
  switch (type) {
    case RAN_MESSAGE_TYPE__SUBSCRIPTION:
      return "SUBSCRIPTION";
    case RAN_MESSAGE_TYPE__INDICATION_REQUEST:
      return "INDICATION_REQUEST";
    case RAN_MESSAGE_TYPE__INDICATION_RESPONSE:
      return "INDICATION_RESPONSE";
    case RAN_MESSAGE_TYPE__CONTROL:
      return "CONTROL";
    case RAN_MESSAGE_TYPE__SOMETHING_ELSE:
      return "SOMETHING_ELSE";
    default:
      return "UNKNOWN";
  }
}

static ue_prb_history_t *lookup_prb_history(rnti_t rnti)
{
  ue_prb_history_t *free_slot = NULL;

  for (size_t i = 0; i < sizeof(prb_history) / sizeof(prb_history[0]); ++i) {
    if (prb_history[i].initialized && prb_history[i].rnti == rnti)
      return &prb_history[i];

    if (!prb_history[i].initialized && free_slot == NULL)
      free_slot = &prb_history[i];
  }

  if (free_slot != NULL) {
    free_slot->rnti = rnti;
    free_slot->initialized = true;
    free_slot->last_dl_rbs = 0;
    free_slot->last_ul_rbs = 0;
    free_slot->last_ts = (struct timespec){0};
  }

  return free_slot;
}

static void update_avg_prbs(gNB_MAC_INST *mac, NR_UE_info_t *UE)
{
  if (mac == NULL || UE == NULL)
    return;

  ue_prb_history_t *hist = lookup_prb_history(UE->rnti);
  if (hist == NULL)
    return;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (hist->last_ts.tv_sec != 0) {
    const double elapsed_s = (double)(now.tv_sec - hist->last_ts.tv_sec)
      + (double)(now.tv_nsec - hist->last_ts.tv_nsec) / 1e9;
    if (elapsed_s > 0.0) {
      const uint64_t dl_total = (uint64_t)UE->mac_stats.dl.total_rbs + UE->mac_stats.dl.total_rbs_retx;
      const uint64_t ul_total = (uint64_t)UE->mac_stats.ul.total_rbs + UE->mac_stats.ul.total_rbs_retx;
      const double slots_per_second = (double)mac->frame_structure.numb_slots_frame * 100.0;
      const double slots_elapsed = elapsed_s * slots_per_second;

      if (slots_elapsed > 0.0) {
        if (dl_total >= hist->last_dl_rbs)
          UE->avg_prbs_dl = (float)((dl_total - hist->last_dl_rbs) / slots_elapsed);
        if (ul_total >= hist->last_ul_rbs)
          UE->avg_prbs_ul = (float)((ul_total - hist->last_ul_rbs) / slots_elapsed);
      }
    }
  }

  hist->last_ts = now;
  hist->last_dl_rbs = (uint64_t)UE->mac_stats.dl.total_rbs + UE->mac_stats.dl.total_rbs_retx;
  hist->last_ul_rbs = (uint64_t)UE->mac_stats.ul.total_rbs + UE->mac_stats.ul.total_rbs_retx;
}

static char *int_to_charray(int value)
{
  char buffer[32];
  int length = snprintf(buffer, sizeof(buffer), "%d", value);
  if (length < 0)
    return NULL;

  size_t bytes = (size_t)length + 1;
  char *out = malloc(bytes);
  if (out == NULL)
    return NULL;

  memcpy(out, buffer, bytes);
  return out;
}

static void free_ue_list_message(UeListM *ue_list)
{
  if (ue_list == NULL)
    return;

  if (ue_list->ue_info != NULL) {
    for (size_t i = 0; i < ue_list->n_ue_info; ++i)
      free(ue_list->ue_info[i]);
    free(ue_list->ue_info);
  }

  free(ue_list);
}

static void free_ran_param_map(RANParamMapEntry **map, size_t count)
{
  if (map == NULL)
    return;

  for (size_t i = 0; i < count; ++i) {
    RANParamMapEntry *entry = map[i];
    if (entry == NULL)
      continue;

    switch (entry->value_case) {
      case RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE:
        free(entry->string_value);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST:
        free_ue_list(entry->ue_list);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_SCHE_CTRL:
        free(entry->sche_ctrl);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_SLICING_CTRL:
        free(entry->slicing_ctrl);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE:
      case RAN_PARAM_MAP_ENTRY__VALUE_BOOL_VALUE:
      case RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET:
        break;
    }

    free(entry);
  }

  free(map);
}

static NR_slice_info_t *lookup_slice_by_sid(NR_Slices_t *info, int16_t sid)
{
  if (info == NULL || !info->initialized)
    return NULL;

  for (uint8_t i = 0; i < info->count; ++i) {
    NR_slice_info_t *entry = info->list[i];
    if (entry != NULL && entry->sid == sid)
      return entry;
  }

  return NULL;
}

static void fill_slice_identity(gNB_MAC_INST *mac, NR_UE_sched_ctrl_t *sched_ctrl, UeInfoM *info)
{
  if (mac == NULL || sched_ctrl == NULL || info == NULL)
    return;

  NR_Slices_t *slice_info = &mac->SL_info;
  int16_t sid = 0;
  for (int idx = 1; idx < sched_ctrl->num_slice_d; ++idx) {
    if (sched_ctrl->avail_slice_list[idx].sid > 0) {
      sid = sched_ctrl->avail_slice_list[idx].sid;
      break;
    }
  }

  pthread_mutex_lock(&slice_info->mutex);
  NR_slice_info_t *slice = lookup_slice_by_sid(slice_info, sid);
  if (slice == NULL && slice_info->count > 0)
    slice = slice_info->list[0];
  if (slice != NULL) {
    info->has_nssai_sst = 1;
    info->nssai_sst = slice->nssai.sst;
    info->has_nssai_sd = 1;
    info->nssai_sd = (int32_t)slice->nssai.sd;
  }
  pthread_mutex_unlock(&slice_info->mutex);
}

static UeInfoM *build_ue_entry(gNB_MAC_INST *mac, NR_UE_info_t *UE)
{
  if (mac == NULL || UE == NULL)
    return NULL;

  update_avg_prbs(mac, UE);

  UeInfoM *entry = malloc(sizeof(*entry));
  if (entry == NULL)
    return NULL;
  ue_info_m__init(entry);

  entry->rnti = UE->rnti;

  entry->has_is_gbr = 1;
  entry->is_gbr = UE->is_GBR;

  entry->has_tbs_dl_toapply = 1;
  entry->tbs_dl_toapply = (float)UE->guaranteed_tbs_bytes_dl;
  entry->has_tbs_ul_toapply = 1;
  entry->tbs_ul_toapply = (float)UE->guaranteed_tbs_bytes_ul;

  entry->has_tbs_avg_dl = 1;
  entry->tbs_avg_dl = UE->avg_tbs_1s_dl;
  entry->has_tbs_avg_ul = 1;
  entry->tbs_avg_ul = UE->avg_tbs_1s_ul;

  float avg_tbs_prb_dl = 0.0f;
  float avg_tbs_prb_ul = 0.0f;
  update_avg_tbs_per_prb(mac->Mod_id, UE->rnti, &UE->mac_stats, &avg_tbs_prb_dl, &avg_tbs_prb_ul);

  entry->has_avg_prbs_dl = 1;
  entry->avg_prbs_dl = UE->avg_prbs_dl;
  entry->has_avg_prbs_ul = 1;
  entry->avg_prbs_ul = UE->avg_prbs_ul;

  entry->has_avg_tbs_per_prb_dl = 1;
  entry->avg_tbs_per_prb_dl = avg_tbs_prb_dl;
  entry->has_avg_tbs_per_prb_ul = 1;
  entry->avg_tbs_per_prb_ul = avg_tbs_prb_ul;

  NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
  entry->has_dl_mac_buffer_occupation = 1;
  entry->dl_mac_buffer_occupation = (float)sched_ctrl->num_total_bytes;
  entry->has_mcs = 1;
  entry->mcs = sched_ctrl->sched_pdsch.mcs;

  NR_mac_stats_t *stats = &UE->mac_stats;
  entry->has_avg_rsrp = 1;
  entry->avg_rsrp = stats->num_rsrp_meas > 0 ? (float)stats->cumul_rsrp / stats->num_rsrp_meas : 0.0f;

  entry->has_ph = 1;
  entry->ph = (float)sched_ctrl->ph;
  entry->has_pcmax = 1;
  entry->pcmax = (float)sched_ctrl->pcmax;

  entry->has_dl_total_bytes = 1;
  entry->dl_total_bytes = (float)stats->dl.total_bytes;
  entry->has_dl_errors = 1;
  entry->dl_errors = (float)stats->dl.errors;
  entry->has_dl_bler = 1;
  entry->dl_bler = sched_ctrl->dl_bler_stats.bler;
  entry->has_dl_mcs = 1;
  entry->dl_mcs = sched_ctrl->dl_bler_stats.mcs;

  entry->has_ul_total_bytes = 1;
  entry->ul_total_bytes = (float)stats->ul.total_bytes;
  entry->has_ul_errors = 1;
  entry->ul_errors = (float)stats->ul.errors;
  entry->has_ul_bler = 1;
  entry->ul_bler = sched_ctrl->ul_bler_stats.bler;
  entry->has_ul_mcs = 1;
  entry->ul_mcs = sched_ctrl->ul_bler_stats.mcs;

  fill_slice_identity(mac, sched_ctrl, entry);

  return entry;
}

UeListM *get_ue_list(void)
{
  UeListM *list = malloc(sizeof(*list));
  if (list == NULL)
    return NULL;
  ue_list_m__init(list);

  size_t capacity = 0;
  size_t total = 0;
  UeInfoM **entries = NULL;

  for (module_id_t mod = 0; mod < RC.nb_nr_macrlc_inst; ++mod) {
    gNB_MAC_INST *mac = RC.nrmac[mod];
    if (mac == NULL)
      continue;

    NR_UEs_t *ue_pool = &mac->UE_info;
    pthread_mutex_lock(&ue_pool->mutex);
    UE_iterator(ue_pool->connected_ue_list, UE) {
      if (total == capacity) {
        size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
        UeInfoM **tmp = realloc(entries, new_capacity * sizeof(*tmp));
        if (tmp == NULL)
          continue;
        entries = tmp;
        capacity = new_capacity;
      }

      UeInfoM *ue_entry = build_ue_entry(mac, UE);
      if (ue_entry != NULL)
        entries[total++] = ue_entry;
    }
    pthread_mutex_unlock(&ue_pool->mutex);
  }

  list->connected_ues = (int32_t)total;
  list->n_ue_info = total;
  list->ue_info = entries;

  return list;
}

void free_ue_list(UeListM *ue_list)
{
  free_ue_list_message(ue_list);
}

void apply_true_gbr(int true_gbr)
{
  if (e2_agent_db == NULL)
    return;

  pthread_mutex_lock(&e2_agent_db->mutex);
  e2_agent_db->true_gbr = true_gbr;
  pthread_mutex_unlock(&e2_agent_db->mutex);

  E2_LOG_I("Updated true_gbr flag to %d\n", true_gbr);
}

void apply_max_cell_prb(int max_prb)
{
  if (e2_agent_db == NULL)
    return;

  pthread_mutex_lock(&e2_agent_db->mutex);
  e2_agent_db->max_prb = max_prb;
  pthread_mutex_unlock(&e2_agent_db->mutex);

  E2_LOG_I("Updated max PRB cap to %d\n", max_prb);
}

static void update_metric_if_present(protobuf_c_boolean has_value, float value, float *target)
{
  if (target == NULL)
    return;
  if (has_value)
    *target = value;
}

static bool apply_single_ue_info(gNB_MAC_INST *mac, const UeInfoM *ue_info)
{
  if (mac == NULL || ue_info == NULL)
    return false;

  NR_UEs_t *ue_pool = &mac->UE_info;
  pthread_mutex_lock(&ue_pool->mutex);

  bool updated = false;
  UE_iterator(ue_pool->connected_ue_list, UE) {
    if (UE->rnti != (rnti_t)ue_info->rnti)
      continue;

    if (ue_info->has_is_gbr)
      UE->is_GBR = ue_info->is_gbr;

    if (ue_info->has_tbs_dl_toapply) {
      float value = ue_info->tbs_dl_toapply;
      if (value < 0)
        value = 0;
      UE->guaranteed_tbs_bytes_dl = (uint32_t)value;
    }

    if (ue_info->has_tbs_ul_toapply) {
      float value = ue_info->tbs_ul_toapply;
      if (value < 0)
        value = 0;
      UE->guaranteed_tbs_bytes_ul = (uint32_t)value;
    }

    update_metric_if_present(ue_info->has_tbs_avg_dl, ue_info->tbs_avg_dl, &UE->avg_tbs_1s_dl);
    update_metric_if_present(ue_info->has_tbs_avg_ul, ue_info->tbs_avg_ul, &UE->avg_tbs_1s_ul);
    update_metric_if_present(ue_info->has_avg_prbs_dl, ue_info->avg_prbs_dl, &UE->avg_prbs_dl);
    update_metric_if_present(ue_info->has_avg_prbs_ul, ue_info->avg_prbs_ul, &UE->avg_prbs_ul);
    update_metric_if_present(ue_info->has_avg_tbs_per_prb_dl,
                            ue_info->avg_tbs_per_prb_dl,
                            &UE->avg_tbs_per_prb_dl);
    update_metric_if_present(ue_info->has_avg_tbs_per_prb_ul,
                            ue_info->avg_tbs_per_prb_ul,
                            &UE->avg_tbs_per_prb_ul);

    updated = true;
    break;
  }

  pthread_mutex_unlock(&ue_pool->mutex);

  return updated;
}

void apply_ue_info(UeListM *ue_list)
{
  if (ue_list == NULL || ue_list->ue_info == NULL)
    return;

  for (size_t idx = 0; idx < ue_list->n_ue_info; ++idx) {
    UeInfoM *info = ue_list->ue_info[idx];
    if (info == NULL)
      continue;

    bool applied = false;
    for (module_id_t mod = 0; mod < RC.nb_nr_macrlc_inst; ++mod) {
      gNB_MAC_INST *mac = RC.nrmac[mod];
      if (mac == NULL)
        continue;
      if (apply_single_ue_info(mac, info)) {
        applied = true;
        break;
      }
    }

    if (!applied)
      E2_LOG_W("UE info update ignored: RNTI %d not found\n", info->rnti);
  }
}

void ran_write(RANParamMapEntry *target_param_map_entry)
{
  if (target_param_map_entry == NULL)
    return;

  switch (target_param_map_entry->key) {
    case RAN_PARAMETER__UE_LIST:
      if (target_param_map_entry->value_case == RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST &&
          target_param_map_entry->ue_list != NULL) {
        apply_ue_info(target_param_map_entry->ue_list);
      } else {
        E2_LOG_W("UE list control missing payload\n");
      }
      break;
    case RAN_PARAMETER__MAX_PRB:
      if (target_param_map_entry->value_case == RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE) {
        apply_max_cell_prb((int)target_param_map_entry->int64_value);
      } else if (target_param_map_entry->value_case == RAN_PARAM_MAP_ENTRY__VALUE_SCHE_CTRL &&
                 target_param_map_entry->sche_ctrl != NULL &&
                 target_param_map_entry->sche_ctrl->has_max_cell_allocable_prbs) {
        apply_max_cell_prb(target_param_map_entry->sche_ctrl->max_cell_allocable_prbs);
      } else {
        E2_LOG_W("MAX_PRB control missing numeric payload\n");
      }
      break;
    case RAN_PARAMETER__USE_TRUE_GBR:
      if (target_param_map_entry->value_case == RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE)
        apply_true_gbr((int)target_param_map_entry->int64_value);
      else
        E2_LOG_W("USE_TRUE_GBR control missing integer payload\n");
      break;
    case RAN_PARAMETER__SLICING_CONTROL:
      if (target_param_map_entry->value_case == RAN_PARAM_MAP_ENTRY__VALUE_SLICING_CTRL &&
          target_param_map_entry->slicing_ctrl != NULL) {
        apply_slicing_ctrl(target_param_map_entry->slicing_ctrl);
      } else {
        E2_LOG_W("Slicing control missing message body\n");
      }
      break;
    default:
      E2_LOG_W("Unhandled control parameter %s (%d)\n",
            param_name(target_param_map_entry->key),
            target_param_map_entry->key);
      break;
  }
}

void ran_read(RANParameter parameter, RANParamMapEntry *entry)
{
  if (entry == NULL)
    return;

  switch (parameter) {
    case RAN_PARAMETER__GNB_ID: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE;
      int gnb_id = 0;
      for (module_id_t mod = 0; mod < RC.nb_nr_macrlc_inst; ++mod) {
        if (RC.nrmac[mod] != NULL) {
          gnb_id = RC.nrmac[mod]->f1_config.gnb_id;
          break;
        }
      }
      entry->string_value = int_to_charray(gnb_id);
      if (entry->string_value == NULL)
        entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
      break;
    }
    case RAN_PARAMETER__SOMETHING: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE;
      entry->string_value = int_to_charray(0);
      if (entry->string_value == NULL)
        entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
      break;
    }
    case RAN_PARAMETER__UE_LIST: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST;
      entry->ue_list = get_ue_list();
      if (entry->ue_list == NULL)
        entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
      break;
    }
    case RAN_PARAMETER__SCHED_INFO_:
    case RAN_PARAMETER__SCHED_CONTROL: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_SCHE_CTRL;
      SchedControlM *ctrl = malloc(sizeof(*ctrl));
      if (ctrl == NULL) {
        entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
        break;
      }
      sched_control_m__init(ctrl);
      if (e2_agent_db != NULL && e2_agent_db->max_prb >= 0) {
        ctrl->has_max_cell_allocable_prbs = 1;
        ctrl->max_cell_allocable_prbs = e2_agent_db->max_prb;
      }
      entry->sche_ctrl = ctrl;
      break;
    }
    case RAN_PARAMETER__MAX_PRB: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE;
      entry->int64_value = e2_agent_db != NULL ? e2_agent_db->max_prb : -1;
      break;
    }
    case RAN_PARAMETER__USE_TRUE_GBR: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_BOOL_VALUE;
      entry->bool_value = e2_agent_db != NULL ? e2_agent_db->true_gbr : 0;
      break;
    }
    case RAN_PARAMETER__SLICING_CONTROL: {
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE_SLICING_CTRL;
      SlicingControlM *ctrl = malloc(sizeof(*ctrl));
      if (ctrl == NULL) {
        entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
        break;
      }
      slicing_control_m__init(ctrl);
      gNB_MAC_INST *mac = NULL;
      for (module_id_t mod = 0; mod < RC.nb_nr_macrlc_inst; ++mod) {
        if (RC.nrmac[mod] != NULL) {
          mac = RC.nrmac[mod];
          break;
        }
      }
      if (mac != NULL) {
        NR_Slices_t *info = &mac->SL_info;
        pthread_mutex_lock(&info->mutex);
        if (info->count > 0 && info->list[0] != NULL) {
          NR_slice_info_t *slice = info->list[0];
          ctrl->sst = slice->nssai.sst;
          ctrl->has_sd = 1;
          ctrl->sd = (int32_t)slice->nssai.sd;
          ctrl->min_ratio = slice->spolicy.min_ratio;
          ctrl->max_ratio = slice->spolicy.max_ratio;
        }
        pthread_mutex_unlock(&info->mutex);
      }
      entry->slicing_ctrl = ctrl;
      break;
    }
    default:
      entry->value_case = RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET;
      E2_LOG_W("Unhandled read parameter %s (%d)\n", param_name(parameter), parameter);
      break;
  }
}

void handle_subscription(RANMessage *message)
{
  if (message == NULL)
    return;

  E2_LOG_I("Subscription message received\n");
  ran_message__free_unpacked(message, NULL);
}

static void handle_control(RANMessage *message)
{
  if (message == NULL)
    return;

  if (message->ran_control_request == NULL) {
    E2_LOG_W("Control message missing request payload\n");
    ran_message__free_unpacked(message, NULL);
    return;
  }

  RANControlRequest *control = message->ran_control_request;
  for (size_t i = 0; i < control->n_target_param_map; ++i) {
    RANParamMapEntry *entry = control->target_param_map[i];
    if (entry == NULL)
      continue;
    E2_LOG_D("Applying control parameter %s\n", param_name(entry->key));
    ran_write(entry);
  }

  ran_message__free_unpacked(message, NULL);
}

void build_indication_response(RANMessage *message, int out_socket, struct sockaddr_in peeraddr)
{
  if (message == NULL || message->ran_indication_request == NULL) {
    if (message != NULL)
      ran_message__free_unpacked(message, NULL);
    return;
  }

  RANIndicationRequest *request = message->ran_indication_request;
  size_t count = request->n_target_params;

  RANParamMapEntry **map = calloc(count, sizeof(*map));
  if (map == NULL) {
    E2_LOG_E("Unable to allocate indication response map\n");
    ran_message__free_unpacked(message, NULL);
    return;
  }

  for (size_t i = 0; i < count; ++i) {
    map[i] = malloc(sizeof(*map[i]));
    if (map[i] == NULL) {
      E2_LOG_E("Allocation failure while building indication response\n");
      count = i;
      break;
    }
    ran_param_map_entry__init(map[i]);
    map[i]->key = request->target_params[i];
    ran_read(map[i]->key, map[i]);
  }

  RANIndicationResponse response = RAN_INDICATION_RESPONSE__INIT;
  response.n_param_map = count;
  response.param_map = map;

  for (size_t i = 0; i < response.n_param_map; ++i) {
    RANParamMapEntry *entry = response.param_map[i];
    if (entry == NULL)
      continue;

    switch (entry->value_case) {
      case RAN_PARAM_MAP_ENTRY__VALUE_INT64_VALUE:
        E2_LOG_I("Indication param %s = %" PRId64 "\n", param_name(entry->key), entry->int64_value);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_STRING_VALUE:
        E2_LOG_I("Indication param %s = %s\n", param_name(entry->key), entry->string_value);
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_BOOL_VALUE:
        E2_LOG_I("Indication param %s = %s\n", param_name(entry->key), entry->bool_value ? "true" : "false");
        break;
      case RAN_PARAM_MAP_ENTRY__VALUE_UE_LIST: {
        E2_LOG_I("Indication param %s : %u connected UEs\n", param_name(entry->key), entry->ue_list->connected_ues);
        for (size_t u = 0; u < entry->ue_list->n_ue_info; ++u) {
          const UeInfoM *ue = entry->ue_list->ue_info[u];
          E2_LOG_I("  UE %04x avg_prbs_dl=%.3f avg_prbs_ul=%.3f dl_bytes=%.0f ul_bytes=%.0f\n",
                   ue != NULL ? ue->rnti : 0,
                   ue != NULL ? ue->avg_prbs_dl : 0.0,
                   ue != NULL ? ue->avg_prbs_ul : 0.0,
                   ue != NULL ? ue->dl_total_bytes : 0.0,
                   ue != NULL ? ue->ul_total_bytes : 0.0);
        }
        break;
      }
      case RAN_PARAM_MAP_ENTRY__VALUE_SCHE_CTRL:
      case RAN_PARAM_MAP_ENTRY__VALUE_SLICING_CTRL:
      case RAN_PARAM_MAP_ENTRY__VALUE__NOT_SET:
        E2_LOG_I("Indication param %s present (complex payload)\n", param_name(entry->key));
        break;
    }
  }

  size_t packed = ran_indication_response__get_packed_size(&response);
  uint8_t *buffer = malloc(packed);
  if (buffer != NULL) {
    ran_indication_response__pack(&response, buffer);
    socklen_t len = sizeof(peeraddr);
    int sent = sendto(out_socket,
                      buffer,
                      packed,
                      MSG_CONFIRM,
                      (const struct sockaddr *)&peeraddr,
                      len);
    if (sent < 0)
      E2_LOG_E("Failed to send indication response (%s)\n", strerror(errno));
    else
      E2_LOG_I("Sent indication response with %zu parameters (%d bytes)\n", count, sent);
    free(buffer);
  } else {
    E2_LOG_E("Unable to allocate buffer for indication response\n");
  }

  free_ran_param_map(map, count);
  ran_message__free_unpacked(message, NULL);
}

void handle_indication_request(RANMessage *message, int out_socket, struct sockaddr_in peeraddr)
{
  if (message == NULL || message->ran_indication_request == NULL) {
    if (message != NULL)
      ran_message__free_unpacked(message, NULL);
    return;
  }

  RANIndicationRequest *request = message->ran_indication_request;
  E2_LOG_I("Indication request for %zu parameters\n", request->n_target_params);
  for (size_t i = 0; i < request->n_target_params; ++i)
    E2_LOG_I("  parameter %d (%s)\n", request->target_params[i], param_name(request->target_params[i]));

  build_indication_response(message, out_socket, peeraddr);
}

static NR_slice_info_t *lookup_slice(NR_Slices_t *info, uint8_t sst, uint32_t sd)
{
  if (info == NULL || !info->initialized)
    return NULL;

  for (uint8_t i = 0; i < info->count; ++i) {
    NR_slice_info_t *entry = info->list[i];
    if (entry != NULL && entry->nssai.sst == sst && entry->nssai.sd == sd)
      return entry;
  }

  return NULL;
}

static bool update_slice_policy_locked(NR_Slices_t *info, NR_slice_info_t *slice, uint8_t min_ratio, uint8_t max_ratio)
{
  if (slice == NULL)
    return false;

  if (min_ratio > max_ratio) {
    E2_LOG_W("Slice %d: requested min_ratio %u larger than max_ratio %u, clamping max_ratio\n",
          slice->sid,
          min_ratio,
          max_ratio);
    max_ratio = min_ratio;
  }

  unsigned total_min = min_ratio;
  for (uint8_t i = 0; i < info->count; ++i) {
    NR_slice_info_t *entry = info->list[i];
    if (entry == NULL || entry->sid == slice->sid)
      continue;
    total_min += entry->spolicy.min_ratio;
  }

  if (total_min > 100) {
    E2_LOG_W("Slice %d update rejected: aggregate minimum ratio would become %u%% (>100%%)\n",
          slice->sid,
          total_min);
    return false;
  }

  slice->spolicy.min_ratio = min_ratio;
  slice->spolicy.max_ratio = max_ratio;
  return true;
}

static bool apply_slicing_ctrl_to_mac(gNB_MAC_INST *mac, const SlicingControlM *ctrl)
{
  if (mac == NULL || ctrl == NULL)
    return false;

  NR_Slices_t *info = &mac->SL_info;
  uint32_t sd = ctrl->has_sd ? (uint32_t)ctrl->sd : 0x00ffffffu;

  pthread_mutex_lock(&info->mutex);
  NR_slice_info_t *slice = lookup_slice(info, (uint8_t)ctrl->sst, sd);
  if (slice == NULL) {
    pthread_mutex_unlock(&info->mutex);
    E2_LOG_W("Slicing control ignored: NSSAI %u.0x%06" PRIx32 " not configured on module %u\n",
          ctrl->sst,
          sd,
          mac->Mod_id);
    return false;
  }

  uint8_t min_ratio = ctrl->min_ratio;
  uint8_t max_ratio = ctrl->max_ratio;
  bool applied = update_slice_policy_locked(info, slice, min_ratio, max_ratio);
  pthread_mutex_unlock(&info->mutex);

  if (applied) {
    E2_LOG_I("Updated slice policy for module %u: NSSAI %u.0x%06" PRIx32 " min=%u max=%u\n",
          mac->Mod_id,
          ctrl->sst,
          sd,
          min_ratio,
          max_ratio);
  }

  return applied;
}

void apply_slicing_ctrl(SlicingControlM *slicing_ctrl)
{
  if (slicing_ctrl == NULL)
    return;

  for (module_id_t mod = 0; mod < RC.nb_nr_macrlc_inst; ++mod) {
    gNB_MAC_INST *mac = RC.nrmac[mod];
    if (mac == NULL)
      continue;
    apply_slicing_ctrl_to_mac(mac, slicing_ctrl);
  }
}

void handle_master_message(void *buf, int buflen, int out_socket, struct sockaddr_in peeraddr)
{
  RANMessage *message = ran_message__unpack(NULL, (size_t)buflen, buf);
  if (message == NULL) {
    E2_LOG_E("Unable to decode RAN message (%d bytes)\n", buflen);
    return;
  }

  E2_LOG_I("Received RAN message type %s (%d)\n",
        message_type_name(message->msg_type),
        message->msg_type);

  switch (message->msg_type) {
    case RAN_MESSAGE_TYPE__SUBSCRIPTION:
      handle_subscription(message);
      break;
    case RAN_MESSAGE_TYPE__INDICATION_REQUEST:
      handle_indication_request(message, out_socket, peeraddr);
      break;
    case RAN_MESSAGE_TYPE__CONTROL:
      handle_control(message);
      break;
    case RAN_MESSAGE_TYPE__INDICATION_RESPONSE:
      E2_LOG_W("Unexpected indication response from peer\n");
      ran_message__free_unpacked(message, NULL);
      break;
    default:
      E2_LOG_W("No handler for RAN message type %d\n", message->msg_type);
      ran_message__free_unpacked(message, NULL);
      break;
  }
}
