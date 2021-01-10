#include "on_lane_forward_simulator.hpp"
#include "math/coordinate_transformer.hpp"
namespace planning {

bool OnLaneForwardSimulator::ForwardOneStep(const Agent &agent,
                                            const SimulationParams &params,
                                            const ReferenceLine &reference_line,
                                            const Agent &leading_agent,
                                            double sim_time_step,
                                            planning_msgs::TrajectoryPoint &point) {
  params_ = params;
  if (!agent.is_valid()) {
    return false;
  }
//  params_.idm_params.PrintParams();
  std::array<double, 3> s_conditions{0, 0, 0};
  std::array<double, 3> d_conditions{0, 0, 0};
  if (!GetAgentFrenetState(agent, reference_line, s_conditions, d_conditions)) {
    return false;
  }

//  ROS_WARN("[OnLaneForwardSimulator::ForwardOneStep], the init ref_point is %f, %f",
//           reference_line.GetReferencePoint(agent.state().x, agent.state().y).x(),
//           reference_line.GetReferencePoint(agent.state().x, agent.state().y).y());
  double lateral_approach_ratio = params.default_lateral_approach_ratio;
  if (!reference_line.IsOnLane({s_conditions[0], d_conditions[0]})) {
    lateral_approach_ratio = params.cutting_in_lateral_approach_ratio;
  }
  double lon_acc = 0.0;
  if (!GetIDMLonAcc(s_conditions, reference_line, leading_agent, lon_acc)) {
    return false;
  }
  double next_s = s_conditions[0] + s_conditions[1] * sim_time_step + 0.5 * lon_acc * sim_time_step * sim_time_step;
  double next_sd = s_conditions[1] + lon_acc * sim_time_step;
  double next_sdd = lon_acc;
  double next_d = std::fabs(s_conditions[1]) < 1e-2 ? d_conditions[0] : d_conditions[0] * lateral_approach_ratio;
  Eigen::Vector2d next_xy;
  if (!reference_line.SLToXY({next_s, next_d}, &next_xy)) {
    return false;
  }
  point.vel = next_sd;
  point.acc = next_sdd;
  point.path_point.x = next_xy.x();
  point.path_point.y = next_xy.y();
  point.path_point.theta =
      common::MathUtils::NormalizeAngle(std::atan2(next_xy[1] - agent.state().y, next_xy[0] - agent.state().x));
  point.path_point.s = next_s;
  point.path_point.kappa = 0.0;
  point.path_point.dkappa = 0.0;


////  ROS_WARN("[OnLaneForwardSimulator]: the lon acc is : %f", lon_acc);
//  std::array<double, 3> next_s_conditions{0, 0, 0};
//  std::array<double, 3> next_d_conditions{0, 0, 0};
//  OnLaneForwardSimulator::AgentMotionModel(s_conditions,
//                                           d_conditions,
//                                           lateral_approach_ratio,
//                                           lon_acc,
//                                           sim_time_step,
//                                           next_s_conditions,
//                                           next_d_conditions);
//  OnLaneForwardSimulator::FrenetStateToTrajectoryPoint(next_s_conditions, next_d_conditions, reference_line, point);
  return true;
}

bool OnLaneForwardSimulator::GetIDMLonAcc(const std::array<double, 3> &ego_s_conditions,
                                          const ReferenceLine &reference_line,
                                          const Agent &leading_agent,
                                          double &lon_acc) const {
  std::array<double, 3> leading_s_conditions{0.0, 0.0, 0.0};
  std::array<double, 3> leading_d_conditions{0.0, 0.0, 0.0};
  double desired_min_gap = 0.0;
  const double ego_lon_a = ego_s_conditions[2];
  const double ego_lon_v = ego_s_conditions[1];
  const double v0 = params_.idm_params.desired_velocity;
  double s_a = 0.0;
  const double s0 = params_.idm_params.s0;
  const double s1 = params_.idm_params.s1;
  const double T = params_.idm_params.safe_time_headway;
  const double a = params_.idm_params.max_acc;
  const double b = params_.idm_params.max_decel;
  if (ego_s_conditions[0] > reference_line.Length() || ego_s_conditions[0] < 0.0) {
    return false;
  }
  if (leading_agent.is_valid()) {
    if (!OnLaneForwardSimulator::GetAgentFrenetState(leading_agent,
                                                     reference_line,
                                                     leading_s_conditions,
                                                     leading_d_conditions)) {
      return false;
    }
    const double leading_vel = leading_s_conditions[1];
    const double delta_v = ego_lon_v - leading_vel;
    desired_min_gap = s0 + s1 * std::sqrt(ego_lon_v / v0)
        + T * ego_lon_v + (ego_lon_v * delta_v) / (2.0 * std::sqrt(a * b));
    s_a = leading_s_conditions[0] - ego_s_conditions[0] + params_.idm_params.leading_vehicle_length;
  } else {
    if (ego_s_conditions[0] + 50.0 > reference_line.Length()) {
      // a virtual static agent in front.
      leading_s_conditions[0] = reference_line.Length() - 0.5;
      leading_s_conditions[1] = 0.0;
      leading_s_conditions[2] = 0.0;
    } else {
      // a virtual agent in front, has same speed and same acc as agent.
      leading_s_conditions[0] = reference_line.Length() - 0.5;
      leading_s_conditions[1] = ego_s_conditions[1];
      leading_s_conditions[2] = ego_s_conditions[2];
    }
    const double leading_vel = leading_s_conditions[1];
    const double delta_v = ego_lon_v - leading_vel;
    desired_min_gap = s0 + s1 * std::sqrt(ego_lon_v / v0)
        + T * ego_lon_v + (ego_lon_v * delta_v) / (2.0 * std::sqrt(a * b));
    s_a = leading_s_conditions[0] - ego_s_conditions[0] + params_.idm_params.leading_vehicle_length;
  }
  lon_acc =
      a * (1 - std::pow(ego_lon_v / v0, params_.idm_params.acc_exponent) - std::pow(desired_min_gap / s_a, 2));
  lon_acc = std::max(std::min(lon_acc, params_.idm_params.max_acc), -params_.idm_params.max_decel);
  return true;
}

planning_msgs::PathPoint OnLaneForwardSimulator::AgentStateToPathPoint(
    const vehicle_state::KinoDynamicState &kino_dynamic_state) {
  planning_msgs::PathPoint path_point;
  path_point.x = kino_dynamic_state.x;
  path_point.y = kino_dynamic_state.y;
  path_point.theta = kino_dynamic_state.theta;
  path_point.kappa = kino_dynamic_state.kappa;
  return path_point;
}

bool OnLaneForwardSimulator::GetAgentFrenetState(const Agent &agent,
                                                 const ReferenceLine &reference_line,
                                                 std::array<double, 3> &s_conditions,
                                                 std::array<double, 3> &d_conditions) {
  double rs = 0.0;
  planning::ReferencePoint ref_point;
  if (!reference_line.GetMatchedPoint(agent.state().x, agent.state().y, &ref_point, &rs)) {
    return false;
  }
  double rx = ref_point.x();
  double ry = ref_point.y();
  double rtheta = ref_point.theta();
  double rkappa = ref_point.kappa();
  double rdkappa = ref_point.dkappa();
  double x = agent.state().x;
  double y = agent.state().y;
  double v = agent.state().v;
  double a = agent.state().a;
  double kappa = agent.state().kappa;
  double theta = agent.state().theta;
  common::CoordinateTransformer::CartesianToFrenet(rs, rx, ry, rtheta,
                                                   rkappa, rdkappa,
                                                   x, y, v, a,
                                                   theta, kappa,
                                                   &s_conditions,
                                                   &d_conditions);
  return true;
}

void OnLaneForwardSimulator::AgentMotionModel(const std::array<double, 3> &s_conditions,
                                              const std::array<double, 3> &d_conditions,
                                              double lateral_approach_ratio,
                                              double lon_acc,
                                              double delta_t,
                                              std::array<double, 3> &next_s_conditions,
                                              std::array<double, 3> &next_d_conditions) {
  next_s_conditions[0] = s_conditions[0] + s_conditions[1] * delta_t + 0.5 * delta_t * delta_t * lon_acc;
  next_s_conditions[1] = s_conditions[1] + delta_t * lon_acc;
  next_s_conditions[2] = lon_acc;
  const double ds = s_conditions[1] * delta_t + 0.5 * delta_t * delta_t * lon_acc;
  if (std::fabs(ds) < 1e-3) {
    next_d_conditions[0] = d_conditions[0];
    next_d_conditions[1] = 0.0;
    next_d_conditions[2] = 0.0;
  } else {
    next_d_conditions[0] = d_conditions[0] * lateral_approach_ratio;
    next_d_conditions[1] = (next_d_conditions[0] - d_conditions[0]) / ds;
    next_d_conditions[2] = (next_d_conditions[1] - d_conditions[1]) / ds;
  }
}

void OnLaneForwardSimulator::FrenetStateToTrajectoryPoint(const std::array<double, 3> &s_conditions,
                                                          const std::array<double, 3> &d_conditions,
                                                          const ReferenceLine &ref_line,
                                                          planning_msgs::TrajectoryPoint &trajectory_point) {

  auto ref_point = ref_line.GetReferencePoint(s_conditions[0]);

  common::CoordinateTransformer::FrenetToCartesian(s_conditions[0],
                                                   ref_point.x(),
                                                   ref_point.y(),
                                                   ref_point.theta(),
                                                   ref_point.kappa(),
                                                   ref_point.dkappa(),
                                                   s_conditions,
                                                   d_conditions,
                                                   &trajectory_point.path_point.x,
                                                   &trajectory_point.path_point.y,
                                                   &trajectory_point.path_point.theta,
                                                   &trajectory_point.path_point.kappa,
                                                   &trajectory_point.vel,
                                                   &trajectory_point.acc);
}

}