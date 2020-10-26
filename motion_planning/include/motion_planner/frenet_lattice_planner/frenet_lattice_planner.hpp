#ifndef CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MOTION_PLANNER_FRENET_LATTICE_PLANNER_FRENET_LATTICE_PLANNER_HPP_
#define CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MOTION_PLANNER_FRENET_LATTICE_PLANNER_FRENET_LATTICE_PLANNER_HPP_

#include <planning_msgs/TrajectoryPoint.h>
#include <planning_msgs/Trajectory.h>
#include "motion_planner/trajectory_planner.hpp"
#include "planning_context.hpp"
#include "collision_checker/st_graph.hpp"
#include "end_condition_sampler.hpp"
#include "quartic_polynomial.hpp"
#include "quintic_polynomial.hpp"
#include "thread_pool.hpp"

namespace planning {

class FrenetLatticePlanner : public TrajectoryPlanner {
 public:

  FrenetLatticePlanner() = default;
  explicit FrenetLatticePlanner(ThreadPool *thread_pool);

  ~FrenetLatticePlanner() override = default;

  /**
   * @brief: main function
   * @param[in] init_trajectory_point
   * @param[in] reference_lines
   * @param[in] maneuver_goal
   * @param[out] pub_trajectory
   * @return
   */
  bool Process(const planning_msgs::TrajectoryPoint &init_trajectory_point,
               const ManeuverGoal &maneuver_goal,
               planning_msgs::Trajectory &pub_trajectory,
               std::vector<planning_msgs::Trajectory> *valid_trajectories) override;

 protected:

  static void GenerateEmergencyStopTrajectory(const planning_msgs::TrajectoryPoint &init_trajectory_point,
                                              planning_msgs::Trajectory &stop_trajectory);
  /**
   * @brief: generate lon trajectories and lat trajectories
   * @param maneuver_info: maneuver goal, comes from maneuver planner
   * @param[out] ptr_lon_traj_vec: lon trajectories
   * @param[out] ptr_lat_traj_vec: lat trajectories
   */
  bool PlanningOnRef(const planning_msgs::TrajectoryPoint &init_trajectory_point,
                     const ManeuverInfo &maneuver_info,
                     std::pair<planning_msgs::Trajectory, double> &optimal_trajectory,
                     std::vector<planning_msgs::Trajectory> *valid_trajectories) const;

  /**
   * @brief: combine the lon and lat trajectories
   * @param ptr_ref_line: reference line
   * @param lon_traj: lon trajectories
   * @param lat_traj: lat trajectories
   * @param ptr_combined_pub_traj: combined trajectory
   * @return
   */
  static planning_msgs::Trajectory CombineTrajectories(const std::shared_ptr<ReferenceLine> &ptr_ref_line,
                                                       const Polynomial &lon_traj,
                                                       const Polynomial &lat_traj,
                                                       double start_time);

 private:

  static void GetInitCondition(const std::shared_ptr<ReferenceLine> &ptr_ref_line,
                               const planning_msgs::TrajectoryPoint &init_trajectory_point,
                               std::array<double, 3> *const init_s,
                               std::array<double, 3> *const init_d);

  /**
   * @brief: generate lat polynomial trajectories
   * @param ptr_lat_traj_vec
   */
  static void GenerateLatTrajectories(const std::array<double, 3> &init_d,
                                      const std::shared_ptr<EndConditionSampler> &end_condition_sampler,
                                      std::vector<std::shared_ptr<Polynomial>> *ptr_lat_traj_vec);

  /**
   *
   * @param maneuver_info
   * @param ptr_lon_traj_vec
   */
  static void GenerateLonTrajectories(const ManeuverInfo &maneuver_info,
                                      const std::array<double, 3> &init_s,
                                      const std::shared_ptr<EndConditionSampler> &end_condition_sampler,
                                      std::vector<std::shared_ptr<Polynomial>> *ptr_lon_traj_vec);

  /**
   * @brief: generate cruising lon trajectories
   * @param cruise_speed:
   * @param ptr_lon_traj_vec
   */
  static void GenerateCruisingLonTrajectories(double cruise_speed,
                                              const std::array<double, 3> &init_s,
                                              const std::shared_ptr<EndConditionSampler> &end_condition_sampler,
                                              std::vector<std::shared_ptr<Polynomial>> *ptr_lon_traj_vec);

  /**
   * @brief: generate stopping lon trajectories
   * @param stop_s: stop position
   * @param ptr_lon_traj_vec
   */
  static void GenerateStoppingLonTrajectories(double stop_s,
                                              const std::array<double, 3> &init_s,
                                              const std::shared_ptr<EndConditionSampler> &end_condition_sampler,
                                              std::vector<std::shared_ptr<Polynomial>> *ptr_lon_traj_vec);

  /**
   * @brief: generate overtake and following lon trajectories
   * @param ptr_lon_traj_vec
   */
  static void GenerateOvertakeAndFollowingLonTrajectories(const std::array<double, 3> &init_s,
                                                          const std::shared_ptr<EndConditionSampler> &end_condition_sampler,
                                                          std::vector<std::shared_ptr<Polynomial>> *ptr_lon_traj_vec);

  /**
   * @brief: generate polynomial trajectories, quartic_polynomial or quintic_polynomial
   * @param[in] init_condition: initial conditions
   * @param[in] end_conditions: end conditions
   * @param[in] order: order: 4-->quartic polynomial, 5-->quintic polynomial
   * @param[out] ptr_traj_vec: polynomial trajectories
   */
  static void GeneratePolynomialTrajectories(const std::array<double, 3> &init_condition,
                                             const std::vector<std::pair<std::array<double, 3>,
                                                                         double>> &end_conditions,
                                             size_t order,
                                             std::vector<std::shared_ptr<Polynomial>> *ptr_traj_vec);
 private:
  ThreadPool *thread_pool_ = nullptr;
};

}
#endif