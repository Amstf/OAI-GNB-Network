#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

extern "C" {
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
}

namespace {

std::string write_policy_file(const std::string &payload)
{
  char path[] = "/tmp/nr_slice_policyXXXXXX";
  int fd = mkstemp(path);
  if (fd < 0)
    return std::string();

  ssize_t written = write(fd, payload.data(), payload.size());
  close(fd);
  if (written != static_cast<ssize_t>(payload.size())) {
    unlink(path);
    return std::string();
  }

  return std::string(path);
}

class SliceConfigFixture : public ::testing::Test {
protected:
  void SetUp() override { memset(&mac_, 0, sizeof(mac_)); }

  void TearDown() override
  {
    nr_mac_free_slice_list(&mac_.SL_info);
    free(mac_.SliceConfigFile);
    mac_.SliceConfigFile = nullptr;
  }

  gNB_MAC_INST mac_{};
};

TEST_F(SliceConfigFixture, ConfigureDefaultSlice)
{
  nssai_t configured[1] = {{.sst = 1, .sd = 0x00112233}};
  nr_mac_configure_slices(&mac_, configured, 1, nullptr);

  ASSERT_EQ(mac_.dl_num_slice, 2);
  ASSERT_EQ(mac_.SL_info.count, 2);
  ASSERT_NE(mac_.SL_info.list[0], nullptr);
  ASSERT_NE(mac_.SL_info.list[1], nullptr);
  EXPECT_EQ(mac_.SL_info.list[0]->sid, 0);
  EXPECT_EQ(mac_.SL_info.list[1]->sid, 1);
  EXPECT_EQ(mac_.SL_info.list[1]->nssai.sst, configured[0].sst);
  EXPECT_EQ(mac_.SL_info.list[1]->nssai.sd, configured[0].sd);
}

TEST_F(SliceConfigFixture, ApplyPolicyUpdatesSlice)
{
  nssai_t configured[1] = {{.sst = 1, .sd = 0x00010203}};
  nr_mac_configure_slices(&mac_, configured, 1, nullptr);

  std::string file = write_policy_file(
      "{\"rrmPolicyRatio\":[{\"sst\":1,\"sd\":66051,\"min_ratio\":30,\"max_ratio\":80,\"dedicated_ratio\":10}]}\n");
  ASSERT_FALSE(file.empty());

  ASSERT_TRUE(nr_mac_load_slice_policy(&mac_.SL_info, file.c_str()));
  unlink(file.c_str());

  NR_slice_info_t *slice = mac_.SL_info.list[1];
  ASSERT_NE(slice, nullptr);
  EXPECT_EQ(slice->spolicy.min_ratio, 30);
  EXPECT_EQ(slice->spolicy.max_ratio, 80);
  EXPECT_EQ(slice->spolicy.dedicated_ratio, 10);
}

TEST_F(SliceConfigFixture, OverflowResetsPolicies)
{
  nssai_t configured[1] = {{.sst = 1, .sd = 0x00010203}};
  nr_mac_configure_slices(&mac_, configured, 1, nullptr);

  std::string file = write_policy_file(
      "{\"rrmPolicyRatio\":[{\"sst\":0,\"sd\":16777215,\"min_ratio\":60,\"max_ratio\":100,\"dedicated_ratio\":0},{\"sst\":1,\"sd\":66051,\"min_ratio\":60,\"max_ratio\":90,\"dedicated_ratio\":0}]}\n");
  ASSERT_FALSE(file.empty());

  ASSERT_TRUE(nr_mac_load_slice_policy(&mac_.SL_info, file.c_str()));
  unlink(file.c_str());

  for (uint8_t i = 0; i < mac_.SL_info.count; ++i) {
    NR_slice_info_t *slice = mac_.SL_info.list[i];
    ASSERT_NE(slice, nullptr);
    EXPECT_EQ(slice->spolicy.min_ratio, 0);
    EXPECT_EQ(slice->spolicy.dedicated_ratio, 0);
  }
}

} // namespace
