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

#include "nr_mac_gNB.h"

#include "common/ran_context.h"
#include "common/utils/LOG/log.h"

#include <errno.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <string.h>

static NR_slice_info_t *find_slice_by_nssai(NR_Slices_t *info, uint8_t sst, uint32_t sd)
{
  if (info == NULL)
    return NULL;

  for (uint8_t i = 0; i < info->count; ++i) {
    NR_slice_info_t *entry = info->list[i];
    if (entry != NULL && entry->nssai.sst == sst && entry->nssai.sd == sd)
      return entry;
  }

  return NULL;
}

static void default_slice_policy(NR_slice_prb_policy_t *policy)
{
  policy->dedicated_ratio = 0;
  policy->min_ratio = 0;
  policy->max_ratio = 100;
}

static char *duplicate_path(const char *src)
{
  if (src == NULL)
    return NULL;

  size_t len = strlen(src) + 1;
  char *dst = malloc(len);
  if (dst == NULL)
    return NULL;

  memcpy(dst, src, len);
  return dst;
}

void nr_mac_init_slice_list(NR_Slices_t *info)
{
  AssertFatal(info != NULL, "Slice container must not be NULL\n");

  if (info->initialized)
    pthread_mutex_destroy(&info->mutex);

  int rc = pthread_mutex_init(&info->mutex, NULL);
  AssertFatal(rc == 0, "Failed to initialise slice mutex (%d)\n", rc);
  info->initialized = true;
  info->count = 0;
  memset(info->list, 0, sizeof(info->list));
}

void nr_mac_free_slice_list(NR_Slices_t *info)
{
  if (info == NULL || !info->initialized)
    return;

  pthread_mutex_lock(&info->mutex);
  for (uint8_t i = 0; i < info->count; ++i) {
    free(info->list[i]);
    info->list[i] = NULL;
  }
  info->count = 0;
  pthread_mutex_unlock(&info->mutex);

  pthread_mutex_destroy(&info->mutex);
  info->initialized = false;
  memset(info->list, 0, sizeof(info->list));
}

NR_slice_info_t *nr_mac_create_slice(NR_Slices_t *info, int16_t sid, const nssai_t *nssai)
{
  AssertFatal(info != NULL, "Slice container must not be NULL\n");
  AssertFatal(info->initialized, "Slice container must be initialised\n");
  AssertFatal(info->count < MAX_NUM_SLICE, "Slice table is full (%u entries)\n", info->count);

  NR_slice_info_t *slice = calloc(1, sizeof(*slice));
  AssertFatal(slice != NULL, "Out of memory while allocating slice\n");

  slice->sid = sid;
  slice->nssai = nssai != NULL ? *nssai : (nssai_t){0, 0xffffff};
  default_slice_policy(&slice->spolicy);

  pthread_mutex_lock(&info->mutex);
  info->list[info->count++] = slice;
  info->list[info->count] = NULL;
  pthread_mutex_unlock(&info->mutex);

  return slice;
}

static void apply_policy(NR_slice_info_t *slice, uint8_t min_ratio, uint8_t max_ratio, uint8_t dedicated_ratio)
{
  if (slice == NULL)
    return;

  if (dedicated_ratio > min_ratio) {
    LOG_W(NR_MAC,
          "Slice %d: dedicated_ratio %u exceeds min_ratio %u, raising min_ratio\n",
          slice->sid,
          dedicated_ratio,
          min_ratio);
    min_ratio = dedicated_ratio;
  }

  if (min_ratio > max_ratio) {
    LOG_W(NR_MAC,
          "Slice %d: max_ratio %u is smaller than min_ratio %u, clamping max_ratio\n",
          slice->sid,
          max_ratio,
          min_ratio);
    max_ratio = min_ratio;
  }

  slice->spolicy.min_ratio = min_ratio;
  slice->spolicy.max_ratio = max_ratio;
  slice->spolicy.dedicated_ratio = dedicated_ratio;
}

bool nr_mac_load_slice_policy(NR_Slices_t *info, const char *file_path)
{
  if (info == NULL || !info->initialized || file_path == NULL)
    return false;

  struct json_object *root = json_object_from_file(file_path);
  if (root == NULL) {
    LOG_W(NR_MAC, "Unable to open slice policy file %s\n", file_path);
    return false;
  }

  struct json_object *ratio_array = NULL;
  if (!json_object_object_get_ex(root, "rrmPolicyRatio", &ratio_array) ||
      json_object_get_type(ratio_array) != json_type_array) {
    LOG_W(NR_MAC, "Slice policy file %s does not contain a valid rrmPolicyRatio array\n", file_path);
    json_object_put(root);
    return false;
  }

  pthread_mutex_lock(&info->mutex);

  for (size_t i = 0; i < json_object_array_length(ratio_array); ++i) {
    struct json_object *entry = json_object_array_get_idx(ratio_array, i);
    if (entry == NULL)
      continue;

    struct json_object *sst_obj = NULL;
    struct json_object *sd_obj = NULL;
    struct json_object *min_obj = NULL;
    struct json_object *max_obj = NULL;
    struct json_object *dedicated_obj = NULL;

    if (!json_object_object_get_ex(entry, "sst", &sst_obj) ||
        !json_object_object_get_ex(entry, "min_ratio", &min_obj) ||
        !json_object_object_get_ex(entry, "max_ratio", &max_obj) ||
        !json_object_object_get_ex(entry, "dedicated_ratio", &dedicated_obj)) {
      LOG_W(NR_MAC, "Ignoring malformed slice entry in %s\n", file_path);
      continue;
    }

    uint8_t sst = (uint8_t)json_object_get_int(sst_obj);
    uint32_t sd = 0xffffff;
    if (json_object_object_get_ex(entry, "sd", &sd_obj)) {
      if (json_object_get_type(sd_obj) == json_type_string) {
        const char *sd_str = json_object_get_string(sd_obj);
        if (sd_str != NULL) {
          char *end = NULL;
          errno = 0;
          unsigned long parsed = strtoul(sd_str, &end, 16);
          if (errno == 0 && end != sd_str && parsed <= 0xffffff)
            sd = (uint32_t)parsed;
          else
            LOG_W(NR_MAC,
                  "Slice policy entry uses invalid sd '%s'; keeping default 0x%06x\n",
                  sd_str,
                  sd);
        }
      } else {
        sd = (uint32_t)json_object_get_int(sd_obj);
      }
    }

    NR_slice_info_t *slice = find_slice_by_nssai(info, sst, sd);
    if (slice == NULL) {
      LOG_W(NR_MAC, "Slice policy for unknown NSSAI %u.0x%06x ignored\n", sst, sd);
      continue;
    }

    uint8_t min_ratio = (uint8_t)json_object_get_int(min_obj);
    uint8_t max_ratio = (uint8_t)json_object_get_int(max_obj);
    uint8_t dedicated_ratio = (uint8_t)json_object_get_int(dedicated_obj);

    apply_policy(slice, min_ratio, max_ratio, dedicated_ratio);
  }

  unsigned int total_min = 0;
  unsigned int total_dedicated = 0;
  for (uint8_t i = 0; i < info->count; ++i) {
    NR_slice_info_t *slice = info->list[i];
    if (slice == NULL)
      continue;
    total_min += slice->spolicy.min_ratio;
    total_dedicated += slice->spolicy.dedicated_ratio;
  }

  if (total_min > 100 || total_dedicated > 100) {
    LOG_W(NR_MAC,
          "Sum of configured ratios exceeds 100%% (min=%u dedicated=%u); resetting policies\n",
          total_min,
          total_dedicated);
    for (uint8_t i = 0; i < info->count; ++i) {
      NR_slice_info_t *slice = info->list[i];
      if (slice != NULL)
        default_slice_policy(&slice->spolicy);
    }
  }

  pthread_mutex_unlock(&info->mutex);
  json_object_put(root);
  return true;
}

void nr_mac_configure_slices(gNB_MAC_INST *mac, const nssai_t *nssai_list, uint16_t num_slices, const char *policy_path)
{
  if (mac == NULL)
    return;

  NR_Slices_t *info = &mac->SL_info;
  nr_mac_free_slice_list(info);
  nr_mac_init_slice_list(info);

  nssai_t default_nssai = {.sst = 0, .sd = 0xffffff};
  NR_slice_info_t *default_slice = nr_mac_create_slice(info, 0, &default_nssai);
  default_slice->spolicy.max_ratio = 100;

  if (num_slices > (MAX_NUM_SLICE - 1)) {
    LOG_W(NR_MAC,
          "Configured S-NSSAI count %u exceeds MAC support (%u); truncating\n",
          num_slices,
          MAX_NUM_SLICE - 1);
    num_slices = MAX_NUM_SLICE - 1;
  }

  for (uint16_t i = 0; i < num_slices; ++i) {
    int16_t sid = (int16_t)(i + 1);
    NR_slice_info_t *slice = nr_mac_create_slice(info, sid, &nssai_list[i]);
    (void)slice;
  }

  mac->dl_num_slice = info->count;

  if (mac->SliceConfigFile != NULL) {
    free(mac->SliceConfigFile);
    mac->SliceConfigFile = NULL;
  }

  if (policy_path != NULL && policy_path[0] != '\0')
    mac->SliceConfigFile = duplicate_path(policy_path);

  if (mac->SliceConfigFile != NULL && !nr_mac_load_slice_policy(info, mac->SliceConfigFile))
    LOG_W(NR_MAC, "Failed to apply slice policy file %s\n", mac->SliceConfigFile);
}

void nr_update_slice_policy(module_id_t module_id)
{
  if (module_id >= RC.nb_nr_macrlc_inst) {
    LOG_W(NR_MAC, "nr_update_slice_policy: module %u out of range\n", module_id);
    return;
  }

  gNB_MAC_INST *mac = RC.nrmac[module_id];
  if (mac == NULL || mac->SliceConfigFile == NULL) {
    LOG_W(NR_MAC, "nr_update_slice_policy: no slice configuration available\n");
    return;
  }

  if (!nr_mac_load_slice_policy(&mac->SL_info, mac->SliceConfigFile))
    LOG_W(NR_MAC, "Runtime slice policy update failed for module %u\n", module_id);
}
