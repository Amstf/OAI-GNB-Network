#include "nr_mac_gNB.h"
#ifdef E2_AGENT
#include "openair2/E2_AGENT/e2_agent_app.h"
#endif

#include <pthread.h>
#include <stdbool.h>

void nr_mac_prepare_slice_budgets(int total_prbs, SLsched_t *slice_sched, int slice_count)
{
  if (slice_sched == NULL || slice_count <= 0)
    return;

  int capped_total = total_prbs;
#ifdef E2_AGENT
  if (e2_agent_db != NULL) {
    pthread_mutex_lock(&e2_agent_db->mutex);
    const int max_prb = e2_agent_db->max_prb;
    pthread_mutex_unlock(&e2_agent_db->mutex);
    if (max_prb >= 0 && capped_total > max_prb)
      capped_total = max_prb;
  }
#endif

  total_prbs = capped_total;

  for (int i = 0; i < slice_count; ++i) {
    NR_slice_info_t *slice = slice_sched[i].slice;
    if (!slice)
      continue;
    slice_sched[i].dedicated_prbs = (total_prbs * slice->spolicy.dedicated_ratio) / 100;
    slice_sched[i].minimum_prbs = (total_prbs * slice->spolicy.min_ratio) / 100;
    slice_sched[i].max_prbs = total_prbs;
    slice_sched[i].used_prbs = 0;
  }

  for (int i = 0; i < slice_count; ++i) {
    NR_slice_info_t *slice_i = slice_sched[i].slice;
    if (!slice_i)
      continue;

    int cap = slice_sched[i].max_prbs;
    for (int j = 0; j < slice_count; ++j) {
      if (i == j)
        continue;
      cap -= slice_sched[j].minimum_prbs;
    }
    if (cap < 0)
      cap = 0;

    int policy_cap = (total_prbs * slice_i->spolicy.max_ratio) / 100;
    if (policy_cap > 0)
      cap = min(cap, policy_cap);

    slice_sched[i].max_prbs = cap;
  }
}

int16_t nr_mac_select_slice_for_ue(const NR_UE_sched_ctrl_t *ctrl, slot_t slot, int ue_index)
{
  if (ctrl == NULL || ctrl->num_slice_d == 0)
    return -1;

  const nr_macrlc_slice_info_t *default_slice = &ctrl->avail_slice_list[0];
  const int16_t default_sid = default_slice->sid;

  if (default_slice->sid < 0)
    return -1;

  if (default_slice->bytes > 0)
    return default_sid;

  int16_t best_sid = default_sid;
  uint32_t best_bytes = 0;

  for (int idx = 1; idx < ctrl->num_slice_d; ++idx) {
    const nr_macrlc_slice_info_t *entry = &ctrl->avail_slice_list[idx];
    if (entry->sid < 0)
      continue;

    if (entry->bytes > best_bytes) {
      best_bytes = entry->bytes;
      best_sid = entry->sid;
    }
  }

  if (best_bytes > 0)
    return best_sid;

  if (ctrl->num_slice_d == 2)
    return ctrl->avail_slice_list[1].sid;

  if (ctrl->num_slice_d >= 3) {
    const bool even_slot = (slot & 0x1) == 0;
    const bool even_ue = (ue_index & 0x1) == 0;
    const int16_t first = ctrl->avail_slice_list[1].sid;
    const int16_t second = ctrl->avail_slice_list[2].sid;
    return (even_slot == even_ue) ? first : second;
  }

  return default_sid;
}
