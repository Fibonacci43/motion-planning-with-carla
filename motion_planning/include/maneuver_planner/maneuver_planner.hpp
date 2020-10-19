#ifndef CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MANEUVER_PLANNER_MANEUVER_PLANNER_HPP_
#define CATKIN_WS_SRC_LOCAL_PLANNER_INCLUDE_MANEUVER_PLANNER_MANEUVER_PLANNER_HPP_

#include <vector>
#include <Eigen/Core>
#include <planning_srvs/Route.h>
#include <planning_msgs/TrajectoryPoint.h>
#include <planning_msgs/Trajectory.h>
#include <planning_srvs/RouteResponse.h>
#include <carla_msgs/CarlaEgoVehicleStatus.h>
#include "state.hpp"
#include "planning_context.hpp"
#include "vehicle_state/vehicle_state.hpp"
#include "reference_line/reference_line.hpp"
#include "motion_planner/trajectory_planner.hpp"

namespace planning {

class State;
class ManeuverPlanner {
 public:
  explicit ManeuverPlanner(const ros::NodeHandle &nh);
  ~ManeuverPlanner();
  void InitPlanner();

  /**
   * @brief: main function
   * @param[in] init_trajectory_point
   * @param[out] pub_trajectory
   * @return
   */
  bool Process(const planning_msgs::TrajectoryPoint &init_trajectory_point,
               planning_msgs::Trajectory::Ptr pub_trajectory);

  /**
   * @return
   */
  int GetLaneId() const { return current_lane_id_; }

  /**
   * @brief: reroute
   * @param[in] start
   * @param[in] destination
   * @param[out] response
   * @return
   */
  bool ReRoute(const geometry_msgs::Pose &start,
               const geometry_msgs::Pose &destination,
               planning_srvs::RouteResponse &response);
  /**
   * @brief: generate reference line
   * @param[in] route
   * @param[in] reference_line
   * @return: true if the reference line generation success, otherwise return false
   */
  static bool GenerateReferenceLine(const planning_srvs::RouteResponse &route,
                                    std::shared_ptr<ReferenceLine> reference_line);

  /**
   * @brief: init_trajectory_point
   * @return
   */
  const planning_msgs::TrajectoryPoint &init_trajectory_point() const;

  /**
   * @brief: set maneuver goal
   * @param maneuver_goal
   */
  void SetManeuverGoal(const ManeuverGoal &maneuver_goal);
  const ManeuverGoal &maneuver_goal() const;
  ManeuverGoal &multable_maneuver_goal();
  std::list<planning_srvs::RouteResponse> &multable_routes() { return routes_; }
  std::list<std::shared_ptr<ReferenceLine>> &multable_ref_line() { return ref_lines_; }

  /**
   *
   * @return
   */
  bool NeedReRoute() const;

 private:

  bool UpdateRouteInfo();

  /**
   * @brief:
   * @param[in] ego_pose
   * @param[in] way_points
   * @return
   */
  static int GetNearestIndex(const geometry_msgs::Pose &ego_pose,
                             const std::vector<planning_msgs::WayPoint> &way_points);

  /**
   * @brief:
   * @param[in] matched_index
   * @param[in] backward_distance
   * @param[in] way_points
   * @return
   */
  static int GetStartIndex(const int matched_index,
                           double backward_distance,
                           const std::vector<planning_msgs::WayPoint> &way_points);

  /**
   * @brief:
   * @param[in] matched_index
   * @param[in] forward_distance
   * @param[in] way_points
   * @return
   */
  static int GetEndIndex(const int matched_index,
                         double forward_distance,
                         const std::vector<planning_msgs::WayPoint> &way_points);

  /**
   * @brief: get waypoints from start to end index on reference line
   * @param[in] start_index
   * @param[in] end_index
   * @param[in] way_points
   * @return
   */
  static std::vector<planning_msgs::WayPoint> GetWayPointsFromStartToEndIndex(const int start_index,
                                                                              const int end_index,
                                                                              const std::vector<planning_msgs::WayPoint> &way_points);

 private:
  ManeuverGoal maneuver_goal_;
  ros::NodeHandle nh_;
  planning_msgs::TrajectoryPoint init_trajectory_point_;
  ros::ServiceClient route_service_client_;
  std::unique_ptr<State> current_state_;
  int current_lane_id_{};
  std::list<planning_srvs::RouteResponse> routes_;
  std::list<std::shared_ptr<ReferenceLine>> ref_lines_;
  ManeuverStatus prev_status_;

};
}

#endif