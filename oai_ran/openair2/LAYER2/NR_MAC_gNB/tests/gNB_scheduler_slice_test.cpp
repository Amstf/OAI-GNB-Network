#include "nr_mac_gNB.h"

#include <gtest/gtest.h>

namespace {

NR_slice_info_t make_slice(int16_t sid, uint8_t min_ratio, uint8_t max_ratio)
{
  NR_slice_info_t info = {};
  info.sid = sid;
  info.spolicy.min_ratio = min_ratio;
  info.spolicy.max_ratio = max_ratio;
  info.spolicy.dedicated_ratio = 0;
  return info;
}

} // namespace

TEST(NRMacSliceBudgetTest, EnforcesPerSliceCaps)
{
  NR_slice_info_t default_slice = make_slice(0, 0, 100);
  NR_slice_info_t slice_a = make_slice(1, 30, 60);
  NR_slice_info_t slice_b = make_slice(2, 20, 50);

  SLsched_t sched[3] = {};
  sched[0].slice = &default_slice;
  sched[1].slice = &slice_a;
  sched[2].slice = &slice_b;

  nr_mac_prepare_slice_budgets(100, sched, 3);

  EXPECT_EQ(sched[1].minimum_prbs, 30);
  EXPECT_EQ(sched[1].max_prbs, 60);
  EXPECT_EQ(sched[2].minimum_prbs, 20);
  EXPECT_EQ(sched[2].max_prbs, 50);
  // Default slice receives the remainder after reserving minima for the others
  EXPECT_EQ(sched[0].minimum_prbs, 0);
  EXPECT_EQ(sched[0].max_prbs, 50);
}

TEST(NRMacSliceBudgetTest, HandlesOverSubscribedMinima)
{
  NR_slice_info_t slice0 = make_slice(0, 60, 100);
  NR_slice_info_t slice1 = make_slice(1, 50, 80);

  SLsched_t sched[2] = {};
  sched[0].slice = &slice0;
  sched[1].slice = &slice1;

  nr_mac_prepare_slice_budgets(80, sched, 2);

  EXPECT_EQ(sched[0].minimum_prbs, 48);
  EXPECT_EQ(sched[1].minimum_prbs, 40);
  EXPECT_EQ(sched[0].max_prbs, 40);
  EXPECT_EQ(sched[1].max_prbs, 32);
}

TEST(NRMacSliceSelectionTest, PrioritisesSignallingWhenBacklogged)
{
  NR_UE_sched_ctrl_t ctrl = {};
  ctrl.num_slice_d = 2;
  ctrl.avail_slice_list[0].sid = 0;
  ctrl.avail_slice_list[0].bytes = 128;
  ctrl.avail_slice_list[1].sid = 1;
  ctrl.avail_slice_list[1].bytes = 512;

  EXPECT_EQ(nr_mac_select_slice_for_ue(&ctrl, /*slot=*/0, /*ue_index=*/0), 0);
}

TEST(NRMacSliceSelectionTest, ChoosesDataSliceWhenControlIdle)
{
  NR_UE_sched_ctrl_t ctrl = {};
  ctrl.num_slice_d = 2;
  ctrl.avail_slice_list[0].sid = 0;
  ctrl.avail_slice_list[0].bytes = 0;
  ctrl.avail_slice_list[1].sid = 3;
  ctrl.avail_slice_list[1].bytes = 256;

  EXPECT_EQ(nr_mac_select_slice_for_ue(&ctrl, /*slot=*/7, /*ue_index=*/1), 3);
}
