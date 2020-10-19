#include "maneuver_planner/emergency_stop.hpp"
#include "maneuver_planner/follow_lane.hpp"

#include <tf/transform_datatypes.h>

#include "obstacle_filter/obstacle_filter.hpp"
#include "reference_line/reference_line.hpp"
#include "planning_config.hpp"
#include "planning_context.hpp"

namespace planning {

bool FollowLane::Enter(ManeuverPlanner *maneuver_planner) {
  ROS_INFO("We are current switching to **FollowLane**...");
  if (maneuver_planner->multable_routes().empty()) {
    auto ego_pose = VehicleState::Instance().pose();
    auto destination = PlanningContext::Instance().global_goal_pose();
    planning_srvs::RouteResponse route_response;
    maneuver_planner->ReRoute(ego_pose, destination.pose, route_response);
    maneuver_planner->multable_routes().push_back(route_response);
  }
  return true;
}

ManeuverStatus FollowLane::Execute(ManeuverPlanner *maneuver_planner) {
  ROS_INFO("We are executing the **LaneFollowState**");
  if (maneuver_planner == nullptr) {
    ROS_ERROR("the ManeuverPlanner is nullptr");
    return ManeuverStatus::kError;
  }
  reference_line_ = maneuver_planner->multable_ref_line().front();

  return ManeuverStatus::kError;
}

void FollowLane::Exit(ManeuverPlanner *maneuver_planner) {
  ROS_INFO("We are currently Exiting the FollowLane...");
}

State &FollowLane::Instance() {
  static FollowLane instance;
  return instance;
}

std::string FollowLane::Name() const { return "FollowLane"; }

State *FollowLane::NextState(ManeuverPlanner *maneuver_planner) const {

  if (maneuver_planner == nullptr) {
    ROS_ERROR("the ManeuverPlanner is nullptr");
    return nullptr;
  }
  ManeuverGoal traffic_maneuver_goal;
  ManeuverGoal obstacle_maneuver_goal;
  // traffic decision
  this->TrafficLightDecision(reference_line_, &traffic_maneuver_goal);
  this->ObstacleDecision(&obstacle_maneuver_goal);
  ManeuverGoal combined_maneuver = CombineManeuver(traffic_maneuver_goal, obstacle_maneuver_goal);
  maneuver_planner->SetManeuverGoal(combined_maneuver);
  switch (combined_maneuver.decision_type) {
    case DecisionType::kStopAtDestination:
    case DecisionType::kStopAtTrafficSign: return &(Stop::Instance());
    case DecisionType::kEmergencyStop: return &(EmergencyStop::Instance());
    case DecisionType::kChangeRight: return &(ChangeRightLane::Instance());
    case DecisionType::kChangeLeft: return &(ChangeLeftLane::Instance());
    case DecisionType::kFollowLane: return &(FollowLane::Instance());
    default: return nullptr;
  }
}

void FollowLane::ObstacleDecision(ManeuverGoal *maneuver_goal) const {
  const auto obstacles = ObstacleFilter::Instance().Obstacles();
  SLPoint ego_sl;
  const double ego_vel = VehicleState::Instance().linear_vel();
  reference_line_->XYToSL(VehicleState::Instance().pose().position.x,
                          VehicleState::Instance().pose().position.y,
                          &ego_sl);
  double forward_clear_distance, backward_clear_distance;
  //  check current lane
  int forward_obstacle_id, backward_obstacle_id;
  this->GetLaneClearDistance(0,
                             reference_line_,
                             &forward_clear_distance,
                             &backward_clear_distance,
                             &forward_obstacle_id,
                             &backward_obstacle_id);

  // determine the road option for vehicle
  auto incoming_way_point = reference_line_->NearestWayPoint(std::min(ego_sl.s + 5.0, reference_line_->Length()));
  double lookahead_distance =
      std::min(std::max(forward_clear_distance - PlanningConfig::Instance().lon_safety_buffer(),
                        PlanningConfig::Instance().min_lookahead_distance()),
               PlanningConfig::Instance().max_lookahead_distance());
  const double reference_line_length = reference_line_->Length();
  // 0. no leading vehicle
  if (forward_obstacle_id < 0) {
    // 0.1 no leading vehicle and near the reference line's end
    if (ego_sl.s + lookahead_distance > reference_line_length) {
      maneuver_goal->maneuver_infos.resize(1);
      maneuver_goal->maneuver_infos.front().maneuver_target.target_s = reference_line_->Length();
      maneuver_goal->maneuver_infos.front().has_stop_point = true;
      maneuver_goal->maneuver_infos.front().lane_id = reference_line_->NearestWayPoint(
          maneuver_goal->maneuver_infos.front().maneuver_target.target_s).lane_id;
      maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
      maneuver_goal->decision_type = DecisionType::kStopAtDestination;
    } else {
      // 0.2 no leading vehicle and not near the reference line's end
      maneuver_goal->maneuver_infos.resize(1);

      maneuver_goal->maneuver_infos.front().maneuver_target.target_speed = PlanningConfig::Instance().target_speed();
//      maneuver_goal->maneuver_infos.front().target_s = ego_sl.s + lookahead_distance;
      maneuver_goal->maneuver_infos.front().lane_id =
          reference_line_->NearestWayPoint(ego_sl.s + lookahead_distance).lane_id;
      maneuver_goal->maneuver_infos.front().has_stop_point = false;
      maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
      maneuver_goal->decision_type = DecisionType::kFollowLane;
    }
    return;
  }
  const auto &forward_obstacle = obstacles.at(forward_obstacle_id);
  // 1. if there is a vehicle is so close
  const double vel_diff = forward_obstacle->Speed() - ego_vel;
  if (forward_clear_distance < PlanningConfig::Instance().lon_safety_buffer()) {
    maneuver_goal->maneuver_infos.resize(1);
    maneuver_goal->maneuver_infos.front().maneuver_target.target_s = ego_sl.s + forward_clear_distance;
//    maneuver_goal->maneuver_infos.front().target_speed = 0.0;
    maneuver_goal->maneuver_infos.front().lane_id =
        reference_line_->NearestWayPoint(maneuver_goal->maneuver_infos.front().maneuver_target.target_s).lane_id;
    maneuver_goal->maneuver_infos.front().has_stop_point = true;
    maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
    maneuver_goal->decision_type = DecisionType::kEmergencyStop;
    return;
  }

  // 2. has leading vehicle

  // 2.1 leading vehicle is far away the ego vehicle
  if (lookahead_distance > PlanningConfig::Instance().max_lookahead_distance()) {
    // 2.1.1 has stop point
    if (ego_sl.s + lookahead_distance > reference_line_length) {
      maneuver_goal->maneuver_infos.resize(1);
      maneuver_goal->maneuver_infos.front().has_stop_point = true;
      maneuver_goal->maneuver_infos.front().maneuver_target.target_s = reference_line_->Length();
//      maneuver_goal->maneuver_infos.front().target_speed = 0.0;
      maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
      maneuver_goal->maneuver_infos.front().lane_id =
          reference_line_->NearestWayPoint(maneuver_goal->maneuver_infos.front().maneuver_target.target_s).lane_id;
      maneuver_goal->decision_type = DecisionType::kStopAtDestination;
    } else {
      // 2.1.2 does not have stop point
      maneuver_goal->maneuver_infos.front().has_stop_point = false;
//      maneuver_goal->maneuver_infos.front().maneuver_target.target_s = ego_sl.s + lookahead_distance;
      maneuver_goal->maneuver_infos.front().maneuver_target.target_speed = PlanningConfig::Instance().target_speed();
      maneuver_goal->maneuver_infos.front().lane_id =
          reference_line_->NearestWayPoint(ego_sl.s + lookahead_distance).lane_id;
      maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
      maneuver_goal->decision_type = DecisionType::kFollowLane;
    }
  } else {
    // 2.2 leading vehicle is in lookahead distance
    if (ego_sl.s + lookahead_distance < reference_line_length) {
      // 2.2.1 check to left and right lane
      if (0.3 * ego_vel > forward_obstacle->Speed()) {
        if (incoming_way_point.road_option.option == planning_msgs::CarlaRoadOption::LANEFOLLOW) {
          this->ChangeLaneDecision(ego_sl.s, forward_clear_distance, backward_clear_distance,
                                   forward_obstacle_id, backward_obstacle_id, incoming_way_point,
                                   maneuver_goal);

        } else {
          // follow the leading vehicle
          maneuver_goal->maneuver_infos.resize(1);
          maneuver_goal->decision_type = DecisionType::kFollowLane;
          maneuver_goal->maneuver_infos.front().lane_id =
              reference_line_->NearestWayPoint(std::min(ego_sl.s + lookahead_distance,
                                                        reference_line_->Length())).lane_id;
          maneuver_goal->maneuver_infos.front().has_stop_point = false;
          maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
          maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
              std::min(forward_obstacle->Speed(), PlanningConfig::Instance().target_speed());
        }
      } else {
        // 2.2.2 the leading vehicle is not so slow, just follow
        maneuver_goal->maneuver_infos.resize(1);
        maneuver_goal->decision_type = DecisionType::kFollowLane;
//        maneuver_goal->maneuver_infos.front().target_s = std::min(ego_sl.s + lookahead_distance, reference_line_->Length());
        maneuver_goal->maneuver_infos.front().lane_id =
            reference_line_->NearestWayPoint(std::min(ego_sl.s + lookahead_distance,
                                                      reference_line_->Length())).lane_id;
        maneuver_goal->maneuver_infos.front().has_stop_point = false;
        maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
        maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
            std::min(forward_obstacle->Speed(), PlanningConfig::Instance().target_speed());
      }
    } else {
      // 2.3 the vehicle is approach the reference end, we don't want to change the lane
      maneuver_goal->maneuver_infos.resize(1);
      maneuver_goal->maneuver_infos.front().has_stop_point = true;
      maneuver_goal->maneuver_infos.front().maneuver_target.target_s = reference_line_length;
      maneuver_goal->maneuver_infos.front().lane_id =
          reference_line_->NearestWayPoint(reference_line_length).lane_id;
      maneuver_goal->decision_type = DecisionType::kStopAtDestination;
    }
  }
}

void FollowLane::ChangeLaneDecision(double ego_s,
                                    double current_lane_forward_clear_distance,
                                    double current_lane_backward_clear_distance,
                                    int current_lane_forward_obstacle_id,
                                    int current_lane_backward_obstacle_id,
                                    const planning_msgs::WayPoint &incoming_way_point,
                                    ManeuverGoal *maneuver_goal) const {
  double left_forward_clear_distance = -10.0;
  double left_backward_clear_distance = -10.0;
  int left_forward_obstacle_id;
  int left_backward_obstacle_id;
  double right_forward_clear_distance = -10.0;
  double right_backward_clear_distance = -10.0;
  int right_forward_obstacle_id;
  int right_backward_obstacle_id;
  bool can_change_left = false;
  bool can_change_right = false;
  const double ego_vel = VehicleState::Instance().linear_vel();
  const auto &rear_obstacle = ObstacleFilter::Instance().Obstacles().at(current_lane_backward_obstacle_id);
  const auto &front_obstacle = ObstacleFilter::Instance().Obstacles().at(current_lane_forward_obstacle_id);
  // check current lane is clear to change lane
  if (current_lane_forward_clear_distance < PlanningConfig::Instance().maneuver_forward_clear_threshold()
      || current_lane_backward_clear_distance < PlanningConfig::Instance().maneuver_backward_clear_threshold()) {
    // current lane is unsafe to change lane, because leading or follow vehicle is so closed
    can_change_right = false;
    can_change_left = false;
  } else {
    // current lane is safe to change lane, check left lane
    if ((incoming_way_point.lane_change.type == planning_msgs::LaneChangeType::LEFT ||
        incoming_way_point.lane_change.type == planning_msgs::LaneChangeType::BOTH) &&
        incoming_way_point.has_left_lane) {
      this->GetLaneClearDistance(-1,
                                 reference_line_,
                                 &left_forward_clear_distance,
                                 &left_backward_clear_distance,
                                 &left_forward_obstacle_id,
                                 &left_backward_obstacle_id);
      if (left_backward_clear_distance > PlanningConfig::Instance().maneuver_target_lane_backward_clear_threshold()
          && left_forward_clear_distance > PlanningConfig::Instance().maneuver_target_lane_forward_clear_threshold()) {
        if (left_forward_obstacle_id < 0) {
          can_change_left = true;
        } else {
          const auto &left_leading_obstacle = ObstacleFilter::Instance().Obstacles().at(left_forward_obstacle_id);
          const double leading_vel = left_leading_obstacle->Speed();
          if (left_backward_obstacle_id < 0) {
            can_change_left = leading_vel > std::min(ego_vel, PlanningConfig::Instance().target_speed());
          } else {
            const auto &left_backward_obstacle = ObstacleFilter::Instance().Obstacles().at(left_backward_obstacle_id);
            const double follow_vel = left_backward_obstacle->Speed();
            can_change_left = follow_vel < std::min(PlanningConfig::Instance().target_speed(), leading_vel);
          }
        }
      }
    } else {
      can_change_left = false;
    }
    // check right lane
    if ((incoming_way_point.lane_change.type == planning_msgs::LaneChangeType::RIGHT ||
        incoming_way_point.lane_change.type == planning_msgs::LaneChangeType::BOTH) &&
        incoming_way_point.has_right_lane) {
      this->GetLaneClearDistance(1,
                                 reference_line_,
                                 &right_forward_clear_distance,
                                 &right_backward_clear_distance,
                                 &right_forward_obstacle_id,
                                 &right_backward_obstacle_id);
      if (right_backward_clear_distance > PlanningConfig::Instance().maneuver_target_lane_backward_clear_threshold()
          && right_forward_clear_distance > PlanningConfig::Instance().maneuver_target_lane_forward_clear_threshold()) {
        if (right_forward_obstacle_id < 0) {
          can_change_right = true;
        } else {
          const auto &right_leading_obstacle = ObstacleFilter::Instance().Obstacles().at(right_forward_obstacle_id);
          const double leading_vel = right_leading_obstacle->Speed();
          if (right_backward_obstacle_id < 0) {
            can_change_right = leading_vel > std::min(ego_vel, PlanningConfig::Instance().target_speed());
          } else {
            const auto &right_backward_obstacle = ObstacleFilter::Instance().Obstacles().at(right_backward_obstacle_id);
            const double follow_vel = right_backward_obstacle->Speed();
            can_change_right = follow_vel < std::min(PlanningConfig::Instance().target_speed(), leading_vel);
          }
        }
      }
    } else {
      can_change_right = false;
    }
  }
  if (!can_change_right && !can_change_left) {
    const double lookahead_distance = std::max(PlanningConfig::Instance().min_lookahead_distance(),
                                               std::min(PlanningConfig::Instance().max_lookahead_distance(),
                                                        current_lane_forward_clear_distance
                                                            - PlanningConfig::Instance().lon_safety_buffer()));
    maneuver_goal->maneuver_infos.resize(1);
    maneuver_goal->decision_type = DecisionType::kFollowLane;
    maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
    maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
        std::min(PlanningConfig::Instance().target_speed(),
                 ObstacleFilter::Instance().Obstacles().at(
                     current_lane_forward_obstacle_id)->Speed());
    maneuver_goal->maneuver_infos.front().has_stop_point = false;
    maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
  } else if (can_change_left && !can_change_right) {
    // only can change left
    const double lookahead_distance = std::max(PlanningConfig::Instance().min_lookahead_distance(),
                                               std::min(PlanningConfig::Instance().max_lookahead_distance(),
                                                        left_forward_clear_distance
                                                            - PlanningConfig::Instance().lon_safety_buffer()));
    /// current lane target;
    maneuver_goal->decision_type = DecisionType::kChangeLeft;
    maneuver_goal->maneuver_infos.resize(2);
    maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
    maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
    maneuver_goal->maneuver_infos.front().has_stop_point = false;
    maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
        ObstacleFilter::Instance().Obstacles().at(current_lane_forward_obstacle_id)->Speed();
    maneuver_goal->maneuver_infos.back().maneuver_target.target_speed = std::min(
        ObstacleFilter::Instance().Obstacles().at(left_forward_obstacle_id)->Speed(),
        PlanningConfig::Instance().target_speed());
    maneuver_goal->maneuver_infos.back().has_stop_point = false;
    maneuver_goal->maneuver_infos.back().lane_id =
        incoming_way_point.lane_id < 0 ? incoming_way_point.lane_id + 1 : incoming_way_point.lane_id - 1;
    maneuver_goal->maneuver_infos.back().ptr_ref_line = nullptr;
  } else if (!can_change_left) {
    // only can change right
    const double lookahead_distance = std::max(PlanningConfig::Instance().min_lookahead_distance(),
                                               std::min(PlanningConfig::Instance().max_lookahead_distance(),
                                                        right_forward_clear_distance
                                                            - PlanningConfig::Instance().lon_safety_buffer()));
    maneuver_goal->maneuver_infos.resize(2);
    // current lane
    maneuver_goal->decision_type = DecisionType::kChangeRight;
    maneuver_goal->maneuver_infos.front().maneuver_target.target_speed = std::min(
        ObstacleFilter::Instance().Obstacles().at(current_lane_forward_obstacle_id)->Speed(),
        PlanningConfig::Instance().target_speed());
    maneuver_goal->maneuver_infos.front().has_stop_point = false;
    maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
    maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
    // target lane
    maneuver_goal->maneuver_infos.back().maneuver_target.target_speed =
        std::min(ObstacleFilter::Instance().Obstacles().at(right_forward_obstacle_id)->Speed(),
                 PlanningConfig::Instance().target_speed());
//    maneuver_goal->target_s = ego_s + lookahead_distance;

    maneuver_goal->maneuver_infos.back().has_stop_point = false;
    maneuver_goal->maneuver_infos.back().ptr_ref_line = nullptr;
    // change right, right lane +1
    maneuver_goal->maneuver_infos.back().lane_id =
        incoming_way_point.lane_id < 0 ? incoming_way_point.lane_id - 1 : incoming_way_point.lane_id + 1;
  } else {
    // can change both
    std::vector<double> leading_velocity;
    leading_velocity.reserve(3);
    std::vector<double> following_velocity;
    following_velocity.reserve(3);
    std::vector<double> leading_clear_distance;
    leading_clear_distance.reserve(3);
    std::vector<double> following_clear_distance;
    following_clear_distance.reserve(3);
    const double left_leading_vel = left_forward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                 : ObstacleFilter::Instance().Obstacles().at(
            left_forward_obstacle_id)->Speed();
    const double left_following_vel = left_backward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                    : ObstacleFilter::Instance().Obstacles().at(
            left_backward_obstacle_id)->Speed();
    const double current_leading_vel = current_lane_forward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                            : ObstacleFilter::Instance().Obstacles().at(
            current_lane_forward_obstacle_id)->Speed();
    const double
        current_following_vel = current_lane_backward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                      : ObstacleFilter::Instance().Obstacles().at(
            current_lane_backward_obstacle_id)->Speed();
    const double right_leading_vel = right_forward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                   : ObstacleFilter::Instance().Obstacles().at(
            right_forward_obstacle_id)->Speed();
    const double right_following_vel = right_forward_obstacle_id < 0 ? PlanningConfig::Instance().target_speed()
                                                                     : ObstacleFilter::Instance().Obstacles().at(
            right_backward_obstacle_id)->Speed();

    leading_velocity.push_back(left_leading_vel);
    leading_velocity.push_back(current_leading_vel);
    leading_velocity.push_back(right_leading_vel);

    following_velocity.push_back(left_following_vel);
    following_velocity.push_back(current_following_vel);
    following_velocity.push_back(right_following_vel);

    leading_clear_distance.push_back(left_forward_clear_distance);
    leading_clear_distance.push_back(current_lane_forward_clear_distance);
    leading_clear_distance.push_back(right_forward_clear_distance);

    following_clear_distance.push_back(left_backward_clear_distance);
    following_clear_distance.push_back(current_lane_backward_clear_distance);
    following_clear_distance.push_back(right_backward_clear_distance);

    int target_lane_offset = SelectLane(ego_s,
                                        ego_vel,
                                        leading_velocity,
                                        following_velocity,
                                        leading_clear_distance,
                                        following_clear_distance);
    switch (target_lane_offset) {
      case -1: {
        // we change left lane
        // 1.current lane
        maneuver_goal->maneuver_infos.resize(2);
        maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
            std::min(leading_velocity[1],
                     PlanningConfig::Instance().target_speed());
        maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
        maneuver_goal->maneuver_infos.front().has_stop_point = false;
        maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
        // 2. left lane
        maneuver_goal->maneuver_infos.back().lane_id =
            incoming_way_point.lane_id < 0 ? incoming_way_point.lane_id + 1 : incoming_way_point.lane_id - 1;
        maneuver_goal->maneuver_infos.back().has_stop_point = false;
        maneuver_goal->maneuver_infos.back().maneuver_target.target_speed =
            std::min(leading_velocity[0], PlanningConfig::Instance().target_speed());
        maneuver_goal->maneuver_infos.back().ptr_ref_line = nullptr;
        maneuver_goal->decision_type = DecisionType::kChangeLeft;
        break;
      }
      case 0 : {
        maneuver_goal->maneuver_infos.resize(1);
        maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
        maneuver_goal->maneuver_infos.front().has_stop_point = false;
        maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
            std::min(leading_velocity[1], PlanningConfig::Instance().target_speed());
        maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
        maneuver_goal->decision_type = DecisionType::kFollowLane;
        break;
      }
      case 1 : {
        maneuver_goal->maneuver_infos.resize(2);
        // 1. current lane
        maneuver_goal->maneuver_infos.front().lane_id = incoming_way_point.lane_id;
        maneuver_goal->maneuver_infos.front().has_stop_point = false;
        maneuver_goal->maneuver_infos.front().maneuver_target.target_speed =
            std::min(leading_velocity[1], PlanningConfig::Instance().target_speed());
        maneuver_goal->maneuver_infos.front().ptr_ref_line = reference_line_;
        // 2. right lane
        maneuver_goal->maneuver_infos.back().lane_id =
            incoming_way_point.lane_id < 0 ? incoming_way_point.lane_id - 1 : incoming_way_point.lane_id + 1;
        maneuver_goal->maneuver_infos.back().has_stop_point = false;
        maneuver_goal->maneuver_infos.back().maneuver_target.target_speed =
            std::min(PlanningConfig::Instance().target_speed(), leading_velocity[2]);
        maneuver_goal->maneuver_infos.back().ptr_ref_line = nullptr;
        maneuver_goal->decision_type = DecisionType::kChangeRight;
        break;
      }
      default:break;
    }
  }
}

int FollowLane::SelectLane(double ego_s,
                           double ego_vel,
                           const std::vector<double> &leading_velocity,
                           const std::vector<double> &following_velocity,
                           const std::vector<double> &leading_clear_distance,
                           const std::vector<double> &following_clear_distance) {

  ROS_ASSERT(!leading_velocity.empty());
  const size_t lane_num = leading_velocity.size();
  ROS_ASSERT(lane_num == following_velocity.size());
  ROS_ASSERT(lane_num == leading_clear_distance.size());
  ROS_ASSERT(lane_num == following_clear_distance.size());
  const double safety_cost_gain = PlanningConfig::Instance().maneuver_safety_cost_gain();
  const double efficiency_cost_gain = PlanningConfig::Instance().maneuver_efficiency_cost_gain();
  const double comfort_cost_gain = PlanningConfig::Instance().maneuver_comfort_cost_gain();
  std::vector<std::pair<int, double>> cost_pairs;
  cost_pairs.reserve(lane_num);
  for (size_t i = 0; i < lane_num; ++i) {
    const double safety_cost = safety_cost_gain * SafetyCost(leading_velocity[i],
                                                             following_velocity[i],
                                                             leading_clear_distance[i],
                                                             following_clear_distance[i]);
    const double efficiency_cost = efficiency_cost_gain * EfficiencyCost(PlanningConfig::Instance().target_speed(),
                                                                         leading_velocity[i],
                                                                         PlanningConfig::Instance().max_lon_velocity());
    const double
        comfort_cost = comfort_cost_gain * ComfortCost(ego_vel, leading_velocity[i], leading_clear_distance[i]);
    const double lane_cost = comfort_cost + efficiency_cost + safety_cost;
    cost_pairs.emplace_back(static_cast<int>(i) - 1, lane_cost);
  }
  auto comp = [](const std::pair<int, double> &rhs, const std::pair<int, double> &lhs) -> bool {
    return rhs.second < lhs.second;
  };
  auto result = std::min_element(cost_pairs.begin(), cost_pairs.end(), comp);
  return result->first;
}

double FollowLane::SafetyCost(double leading_vel,
                              double following_vel,
                              double leading_clear_distance,
                              double following_clear_distance) {

  const double target_lane_vel_diff = leading_vel - following_vel;
  const double change_lane_execute_time = PlanningConfig::Instance().maneuver_execute_time_length();
  const double target_lane_clear_length = following_clear_distance + leading_clear_distance;
  const double clear_length_diff = target_lane_vel_diff * change_lane_execute_time;
  const double min_clear_length = std::max(1e-3, clear_length_diff + target_lane_clear_length);
  const double clear_length_threshold = PlanningConfig::Instance().maneuver_target_lane_forward_clear_threshold()
      + PlanningConfig::Instance().maneuver_target_lane_backward_clear_threshold();
  const double safety_cost = clear_length_threshold / std::min(min_clear_length, clear_length_threshold);
  return safety_cost;
}

double FollowLane::EfficiencyCost(double target_vel,
                                  double leading_vel,
                                  double max_vel) {
  double vel_buffer = std::max(max_vel - target_vel, 0.2);
  double cost = 0.0;
  double desired_vel = max_vel - vel_buffer;
  if (leading_vel < desired_vel) {
    cost = (desired_vel - leading_vel) / desired_vel;
  } else if (leading_vel > desired_vel && leading_vel < max_vel) {
    cost = (leading_vel - desired_vel) / vel_buffer;
  } else {
    cost = std::numeric_limits<double>::infinity();
  }
  return cost;
}

double FollowLane::ComfortCost(double ego_vel,
                               double leading_vel,
                               double forward_clear_distance) {
  const double acc = (leading_vel * leading_vel - ego_vel * ego_vel) /
      2 * std::max(1e-3, forward_clear_distance - PlanningConfig::Instance().lon_safety_buffer());
  const double max_acc = PlanningConfig::Instance().max_lon_acc();
  double cost;
  if (std::fabs(acc) > max_acc) {
    cost = std::numeric_limits<double>::infinity();
  } else {
    cost = std::fabs(acc) / max_acc;
  }
  return cost;
}
}