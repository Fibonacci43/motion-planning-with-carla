#include "maneuver_planner/state.hpp"
#include "maneuver_planner/change_lane_left_state.hpp"

namespace planning {

bool ChangeLaneLeftState::Execute(ManeuverPlanner *maneuver_planner) {

  return false;
}

bool ChangeLaneLeftState::Enter(ManeuverPlanner *maneuver_planner) {
  ROS_INFO("We are current enter the ChangeLaneLeft State");

}

void ChangeLaneLeftState::Exit(ManeuverPlanner *maneuver_planner) {
  ROS_INFO("We are current exit the ChangeLaneLeft State");

}

State &ChangeLaneLeftState::Instance() {
  static ChangeLaneLeftState instance;
  return instance;
}

std::string ChangeLaneLeftState::Name() const { return "ChangeLaneLeftState"; }

State *ChangeLaneLeftState::NextState(ManeuverPlanner *maneuver_planner) const {
  return nullptr;
}

std::vector<StateName> ChangeLaneLeftState::GetPosibileNextStates() const {
  return std::vector<StateName>();
}
}