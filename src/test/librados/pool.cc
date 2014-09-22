#include "include/rados/librados.h"
#include "test/librados/test.h"

#include "gtest/gtest.h"
#include <errno.h>
#include <vector>

#define POOL_LIST_BUF_SZ 32768

TEST(LibRadosPools, PoolList) {
  char pool_list_buf[POOL_LIST_BUF_SZ];
  char *buf = pool_list_buf;
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));
  ASSERT_LT(rados_pool_list(cluster, buf, POOL_LIST_BUF_SZ), POOL_LIST_BUF_SZ);

  bool found_pool = false;
  while (buf[0] != '\0') {
    if ((found_pool == false) && (strcmp(buf, pool_name.c_str()) == 0)) {
      found_pool = true;
    }
    buf += strlen(buf) + 1;
  }
  ASSERT_EQ(found_pool, true);

  // make sure we honor the buffer size limit
  buf = pool_list_buf;
  memset(buf, 0, POOL_LIST_BUF_SZ);
  ASSERT_LT(rados_pool_list(cluster, buf, 20), POOL_LIST_BUF_SZ);
  ASSERT_NE(0, buf[0]);  // include at least one pool name
  ASSERT_EQ(0, buf[20]);  // but don't touch the stopping point

  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

int64_t rados_pool_lookup(rados_t cluster, const char *pool_name);

TEST(LibRadosPools, PoolLookup) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));
  ASSERT_LT(0, rados_pool_lookup(cluster, pool_name.c_str()));
  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

TEST(LibRadosPools, PoolLookup2) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));
  int64_t pool_id = rados_pool_lookup(cluster, pool_name.c_str());
  ASSERT_GT(pool_id, 0);
  rados_ioctx_t ioctx;
  ASSERT_EQ(0, rados_ioctx_create(cluster, pool_name.c_str(), &ioctx));
  int64_t pool_id2 = rados_ioctx_get_id(ioctx);
  ASSERT_EQ(pool_id, pool_id2);
  rados_ioctx_destroy(ioctx);
  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

TEST(LibRadosPools, PoolDelete) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));
  ASSERT_EQ(0, rados_pool_delete(cluster, pool_name.c_str()));
  ASSERT_GT(0, rados_pool_lookup(cluster, pool_name.c_str()));
  ASSERT_EQ(0, rados_pool_create(cluster, pool_name.c_str()));
  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

TEST(LibRadosPools, PoolCreateDelete) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));

  std::string n = pool_name + "abc123";
  ASSERT_EQ(0, rados_pool_create(cluster, n.c_str()));
  ASSERT_EQ(-EEXIST, rados_pool_create(cluster, n.c_str()));
  ASSERT_EQ(0, rados_pool_delete(cluster, n.c_str()));
  ASSERT_EQ(-ENOENT, rados_pool_delete(cluster, n.c_str()));

  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

TEST(LibRadosPools, PoolCreateWithCrushRule) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));

  std::string pool2_name = get_temp_pool_name();
  ASSERT_EQ(0, rados_pool_create_with_crush_rule(cluster,
			    pool2_name.c_str(), 0));
  ASSERT_EQ(0, rados_pool_delete(cluster, pool2_name.c_str()));

  std::string pool3_name = get_temp_pool_name();
  ASSERT_EQ(0, rados_pool_create_with_all(cluster, pool3_name.c_str(),
					  456ull, 0));
  rados_ioctx_t ioctx;
  ASSERT_EQ(0, rados_ioctx_create(cluster, pool3_name.c_str(), &ioctx));
  uint64_t auid;
  ASSERT_EQ(0, rados_ioctx_pool_get_auid(ioctx, &auid));
  ASSERT_EQ(456ull, auid);
  ASSERT_EQ(0, rados_pool_delete(cluster, pool3_name.c_str()));
  rados_ioctx_destroy(ioctx);
  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}

TEST(LibRadosPools, PoolGetTiers) {
  rados_t cluster;
  std::string pool_name = get_temp_pool_name();
  ASSERT_EQ("", create_one_pool(pool_name, &cluster));
  std::string tier_pool_name = pool_name + "-cache";
  ASSERT_EQ(0, rados_pool_create(cluster, tier_pool_name.c_str()));

  int64_t pool_id = rados_pool_lookup(cluster, pool_name.c_str());
  ASSERT_GE(pool_id, 0);

  int64_t tier_pool_id = rados_pool_lookup(cluster, tier_pool_name.c_str());
  ASSERT_GE(tier_pool_id, 0);


  rados_pool_tier_t tiers;
  EXPECT_EQ(0, rados_pool_get_tiers(cluster, pool_id, &tiers));
  EXPECT_EQ(0, tiers.tiers_len);
  EXPECT_EQ(-1, tiers.tier_of);
  EXPECT_EQ(-1, tiers.read_tier);
  EXPECT_EQ(-1, tiers.write_tier);
  EXPECT_EQ(RADOS_CACHEMODE_NONE, tiers.cache_mode);
  free(tiers.tiers);

  std::string cmdstr = "{\"prefix\": \"osd tier add\", \"pool\": \"" +
     pool_name + "\", \"tierpool\":\"" + tier_pool_name + "\", \"force_nonempty\":\"\"}";
  char *cmd[1];
  cmd[0] = (char *)cmdstr.c_str();
  ASSERT_EQ(0, rados_mon_command(cluster, (const char **)cmd, 1, "", 0, NULL, 0, NULL, 0));

  cmdstr = "{\"prefix\": \"osd tier cache-mode\", \"pool\": \"" +
     tier_pool_name + "\", \"mode\":\"readonly\"}";
  cmd[0] = (char *)cmdstr.c_str();
  ASSERT_EQ(0, rados_mon_command(cluster, (const char **)cmd, 1, "", 0, NULL, 0, NULL, 0));

  EXPECT_EQ(0, rados_wait_for_latest_osdmap(cluster));

  EXPECT_EQ(0, rados_pool_get_tiers(cluster, pool_id, &tiers));
  EXPECT_EQ(1, tiers.tiers_len);
  if (tiers.tiers_len > 0) {
      EXPECT_EQ(tier_pool_id, tiers.tiers[0]);
  }
  EXPECT_EQ(-1, tiers.tier_of);
  EXPECT_EQ(-1, tiers.read_tier);
  EXPECT_EQ(-1, tiers.write_tier);
  EXPECT_EQ(RADOS_CACHEMODE_NONE, tiers.cache_mode);
  free(tiers.tiers);

  EXPECT_EQ(0, rados_pool_get_tiers(cluster, tier_pool_id, &tiers));
  EXPECT_EQ(0, tiers.tiers_len);
  EXPECT_EQ(pool_id, tiers.tier_of);
  EXPECT_EQ(-1, tiers.read_tier);
  EXPECT_EQ(-1, tiers.write_tier);
  EXPECT_EQ(RADOS_CACHEMODE_READONLY, tiers.cache_mode);
  free(tiers.tiers);

  cmdstr = "{\"prefix\": \"osd tier remove\", \"pool\": \"" +
     pool_name + "\", \"tierpool\":\"" + tier_pool_name + "\"}";
  cmd[0] = (char *)cmdstr.c_str();
  ASSERT_EQ(0, rados_mon_command(cluster, (const char **)cmd, 1, "", 0, NULL, 0, NULL, 0));
  ASSERT_EQ(0, rados_pool_delete(cluster, tier_pool_name.c_str()));
  ASSERT_EQ(0, destroy_one_pool(pool_name, &cluster));
}
