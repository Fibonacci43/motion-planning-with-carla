#include "maneuver_planner/change_lane_right_state.hpp"
namespace planning {

bool ChangeLaneRightState::Enter(ManeuverPlanner *maneuver_planner) {

}
bool ChangeLaneRightState::Execute(ManeuverPlanner *maneuver_planner) {
  return false;
}
void ChangeLaneRightState::Exit(ManeuverPlanner *maneuver_planner) {

}
State &ChangeLaneRightState::Instance() {
  static ChangeLaneRightState instance;
  return instance;
}
std::string ChangeLaneRightState::Name() const { return "ChangeLaneRightState"; }
State *ChangeLaneRightState::NextState(ManeuverPlanner *maneuver_planner) const {
  return nullptr;
}
std::vector<StateName> ChangeLaneRightState::GetPosibileNextStates() const {
  return std::vector<StateName>();
}
}