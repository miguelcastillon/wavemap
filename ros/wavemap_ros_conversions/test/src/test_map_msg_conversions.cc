#include <gtest/gtest.h>
#include <wavemap/common.h>
#include <wavemap/map/hashed_chunked_wavelet_octree.h>
#include <wavemap/map/hashed_wavelet_octree.h>
#include <wavemap/map/volumetric_data_structure_base.h>
#include <wavemap/map/wavelet_octree.h>
#include <wavemap/test/config_generator.h>
#include <wavemap/test/fixture_base.h>
#include <wavemap/test/geometry_generator.h>
#include <wavemap_msgs/Map.h>

#include "wavemap_ros_conversions/map_msg_conversions.h"

namespace wavemap {
template <typename VolumetricDataStructureType>
class MapMsgConversionsTest : public FixtureBase,
                              public GeometryGenerator,
                              public ConfigGenerator {
 protected:
  void SetUp() override {
    ros::Time::init();
    stamp = ros::Time::now();
  }

  const std::string frame_id = "odom";
  ros::Time stamp{};
  static constexpr FloatingPoint kAcceptableReconstructionError = 5e-2f;
};

using VolumetricDataStructureTypes =
    ::testing::Types<HashedBlocks, WaveletOctree, HashedWaveletOctree,
                     HashedChunkedWaveletOctree>;
TYPED_TEST_SUITE(MapMsgConversionsTest, VolumetricDataStructureTypes, );

TYPED_TEST(MapMsgConversionsTest, MetadataPreservation) {
  const auto config =
      ConfigGenerator::getRandomConfig<typename TypeParam::Config>();

  // Create the original map and make sure it matches the config
  typename TypeParam::ConstPtr map = std::make_shared<TypeParam>(config);
  ASSERT_EQ(map->getMinCellWidth(), config.min_cell_width);
  ASSERT_EQ(map->getMinLogOdds(), config.min_log_odds);
  ASSERT_EQ(map->getMaxLogOdds(), config.max_log_odds);
  if constexpr (!std::is_same_v<TypeParam, HashedBlocks>) {
    ASSERT_EQ(map->getTreeHeight(), config.tree_height);
  }

  // Convert to base pointer
  VolumetricDataStructureBase::ConstPtr map_base = map;
  ASSERT_EQ(map_base->getMinCellWidth(), config.min_cell_width);
  ASSERT_EQ(map_base->getMinLogOdds(), config.min_log_odds);
  ASSERT_EQ(map_base->getMaxLogOdds(), config.max_log_odds);

  // Serialize and deserialize
  wavemap_msgs::Map map_msg;
  ASSERT_TRUE(convert::mapToRosMsg(*map_base, TestFixture::frame_id,
                                   TestFixture::stamp, map_msg));
  VolumetricDataStructureBase::Ptr map_base_round_trip;
  ASSERT_TRUE(convert::rosMsgToMap(map_msg, map_base_round_trip));
  ASSERT_TRUE(map_base_round_trip);

  // Check the header
  EXPECT_EQ(map_msg.header.frame_id, TestFixture::frame_id);
  EXPECT_EQ(map_msg.header.stamp, TestFixture::stamp);

  // TODO(victorr): Add option to deserialize into hashed chunked wavelet
  //                octrees, instead of implicitly converting them to regular
  //                hashed wavelet octrees.
  if constexpr (std::is_same_v<TypeParam, HashedChunkedWaveletOctree>) {
    HashedWaveletOctree::ConstPtr map_round_trip =
        std::dynamic_pointer_cast<HashedWaveletOctree>(map_base_round_trip);
    ASSERT_TRUE(map_round_trip);

    // Check that the metadata still matches the original config
    EXPECT_EQ(map_round_trip->getMinCellWidth(), config.min_cell_width);
    EXPECT_EQ(map_round_trip->getMinLogOdds(), config.min_log_odds);
    EXPECT_EQ(map_round_trip->getMaxLogOdds(), config.max_log_odds);
    EXPECT_EQ(map_round_trip->getTreeHeight(), config.tree_height);
  } else {
    typename TypeParam::ConstPtr map_round_trip =
        std::dynamic_pointer_cast<TypeParam>(map_base_round_trip);
    ASSERT_TRUE(map_round_trip);

    // Check that the metadata still matches the original config
    EXPECT_EQ(map_round_trip->getMinCellWidth(), config.min_cell_width);
    EXPECT_EQ(map_round_trip->getMinLogOdds(), config.min_log_odds);
    EXPECT_EQ(map_round_trip->getMaxLogOdds(), config.max_log_odds);
    if constexpr (!std::is_same_v<TypeParam, HashedBlocks>) {
      EXPECT_EQ(map_round_trip->getTreeHeight(), config.tree_height);
    }
  }
}

TYPED_TEST(MapMsgConversionsTest, InsertionAndLeafVisitor) {
  constexpr int kNumRepetitions = 3;
  for (int i = 0; i < kNumRepetitions; ++i) {
    // Create a random map
    const auto config =
        ConfigGenerator::getRandomConfig<typename TypeParam::Config>();
    TypeParam map_original(config);
    const std::vector<Index3D> random_indices =
        GeometryGenerator::getRandomIndexVector<3>(
            1000u, 2000u, Index3D::Constant(-5000), Index3D::Constant(5000));
    for (const Index3D& index : random_indices) {
      const FloatingPoint update = TestFixture::getRandomUpdate();
      map_original.addToCellValue(index, update);
    }
    map_original.prune();

    // Serialize and deserialize
    wavemap_msgs::Map map_msg;
    ASSERT_TRUE(convert::mapToRosMsg(map_original, TestFixture::frame_id,
                                     TestFixture::stamp, map_msg));
    VolumetricDataStructureBase::Ptr map_base_round_trip;
    ASSERT_TRUE(convert::rosMsgToMap(map_msg, map_base_round_trip));
    ASSERT_TRUE(map_base_round_trip);

    // Check that both maps contain the same leaves
    map_base_round_trip->forEachLeaf(
        [&map_original](const OctreeIndex& node_index,
                        FloatingPoint round_trip_value) {
          if constexpr (std::is_same_v<TypeParam, HashedBlocks>) {
            EXPECT_EQ(node_index.height, 0);
            EXPECT_NEAR(round_trip_value,
                        map_original.getCellValue(node_index.position),
                        TestFixture::kAcceptableReconstructionError);
          } else {
            EXPECT_NEAR(round_trip_value, map_original.getCellValue(node_index),
                        TestFixture::kAcceptableReconstructionError);
          }
        });

    // TODO(victorr): Remove this special case once deserializing directly
    //                into HashedChunkedWaveletOctrees is supported
    if (std::is_same_v<TypeParam, HashedChunkedWaveletOctree>) {
      HashedWaveletOctree::ConstPtr map_round_trip =
          std::dynamic_pointer_cast<HashedWaveletOctree>(map_base_round_trip);
      ASSERT_TRUE(map_round_trip);

      map_original.forEachLeaf([&map_round_trip](const OctreeIndex& node_index,
                                                 FloatingPoint original_value) {
        EXPECT_NEAR(original_value, map_round_trip->getCellValue(node_index),
                    TestFixture::kAcceptableReconstructionError);
      });
    } else {
      typename TypeParam::ConstPtr map_round_trip =
          std::dynamic_pointer_cast<TypeParam>(map_base_round_trip);
      ASSERT_TRUE(map_round_trip);

      map_original.forEachLeaf([&map_round_trip](const OctreeIndex& node_index,
                                                 FloatingPoint original_value) {
        if constexpr (std::is_same_v<TypeParam, HashedBlocks>) {
          EXPECT_EQ(node_index.height, 0);
          EXPECT_NEAR(original_value,
                      map_round_trip->getCellValue(node_index.position),
                      TestFixture::kAcceptableReconstructionError);
        } else {
          EXPECT_NEAR(original_value, map_round_trip->getCellValue(node_index),
                      TestFixture::kAcceptableReconstructionError);
        }
      });
    }
  }
}
}  // namespace wavemap
