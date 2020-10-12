#ifndef CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MOTION_PLANNER_FRENENT_LATTICE_PLANNER_TRAJECTORY_SELECTOR_HPP_
#define CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MOTION_PLANNER_FRENENT_LATTICE_PLANNER_TRAJECTORY_SELECTOR_HPP_

#include <array>
#include <memory>
#include <queue>
#include <ros/ros.h>

#include "polynomial.hpp"
#include "st_graph.hpp"

namespace planning {
class TrajectorySelector {
  typedef std::pair<std::shared_ptr<Polynomial>, std::shared_ptr<Polynomial>> TrajectoryPair;
  typedef std::pair<TrajectoryPair, double> TrajectoryCostPair;

 public:
  TrajectorySelector() = default;
  ~TrajectorySelector() = default;
  /**
   *
   * @param init_s
   * @param lon_trajectory_vec
   * @param lat_trajectory_vec
   * @param ptr_ref_line
   * @param ptr_st_graph
   */
  TrajectorySelector(const std::array<double, 3> &init_s,
                     const std::vector<std::shared_ptr<Polynomial>> &lon_trajectory_vec,
                     const std::vector<std::shared_ptr<Polynomial>> &lat_trajectory_vec,
                     std::shared_ptr<ReferenceLine> ptr_ref_line,
                     std::shared_ptr<STGraph> ptr_st_graph);

 private:

  struct Comparator : public std::binary_function<const TrajectoryCostPair &, const TrajectoryCostPair &, bool> {
    bool operator()(const TrajectoryCostPair &left, const TrajectoryCostPair &right) {
      return left.second > right.second;
    }
  };

 private:
  std::priority_queue<TrajectoryCostPair, std::vector<TrajectoryCostPair>, Comparator> cost_queue_;

  std::array<double, 3> init_s_{0.0, 0.0, 0.0};
  std::shared_ptr<ReferenceLine> ptr_ref_line_;
  std::shared_ptr<STGraph> ptr_st_graph_;

};
}
#endif