#include <cmath>
#include <cassert>
#include <array>
#include "math/math_utils.hpp"
#include "math/coordinate_transformer.hpp"

namespace common {

void CoordinateTransformer::CartesianToFrenet(double rs, double rx,
                                              double ry, double rtheta,
                                              double rkappa, double rdkappa,
                                              double x, double y,
                                              double v, double a,
                                              double theta, double kappa,
                                              std::array<double, 3> *const ptr_s_condition,
                                              std::array<double, 3> *const ptr_d_condition) {
  const double dx = x - rx;
  const double dy = y - ry;

  const double cos_theta_r = std::cos(rtheta);
  const double sin_theta_r = std::sin(rtheta);

  const double cross_rd_nd = cos_theta_r * dy - sin_theta_r * dx;
  ptr_d_condition->at(0) =
      std::copysign(std::sqrt(dx * dx + dy * dy), cross_rd_nd);

  const double delta_theta = theta - rtheta;
  const double tan_delta_theta = std::tan(delta_theta);
  const double cos_delta_theta = std::cos(delta_theta);

  const double one_minus_kappa_r_d = 1 - rkappa * ptr_d_condition->at(0);
  ptr_d_condition->at(1) = one_minus_kappa_r_d * tan_delta_theta;

  const double kappa_r_d_prime =
      rdkappa * ptr_d_condition->at(0) + rkappa * ptr_d_condition->at(1);

  ptr_d_condition->at(2) =
      -kappa_r_d_prime * tan_delta_theta +
          one_minus_kappa_r_d / cos_delta_theta / cos_delta_theta *
              (kappa * one_minus_kappa_r_d / cos_delta_theta - rkappa);

  ptr_s_condition->at(0) = rs;

  ptr_s_condition->at(1) = v * cos_delta_theta / one_minus_kappa_r_d;

  const double delta_theta_prime =
      one_minus_kappa_r_d / cos_delta_theta * kappa - rkappa;
  ptr_s_condition->at(2) =
      (a * cos_delta_theta -
          ptr_s_condition->at(1) * ptr_s_condition->at(1) *
              (ptr_d_condition->at(1) * delta_theta_prime - kappa_r_d_prime)) /
          one_minus_kappa_r_d;

}
void CoordinateTransformer::CartesianToFrenet(double rs,
                                              double rx,
                                              double ry,
                                              double rtheta,
                                              double x,
                                              double y,
                                              double *const ptr_s,
                                              double *const ptr_d) {

  const double dx = x - rx;
  const double dy = y - ry;

  const double cos_theta_r = std::cos(rtheta);
  const double sin_theta_r = std::sin(rtheta);

  const double cross_rd_nd = cos_theta_r * dy - sin_theta_r * dx;
  *ptr_d = std::copysign(std::sqrt(dx * dx + dy * dy), cross_rd_nd);
  *ptr_s = rs;
}
void CoordinateTransformer::FrenetToCartesian(double rs, double rx,
                                              double ry, double rtheta,
                                              double rkappa, double rdkappa,
                                              const std::array<double, 3> &s_condition,
                                              const std::array<double, 3> &d_condition,
                                              double *ptr_x, double *ptr_y,
                                              double *ptr_theta, double *ptr_kappa,
                                              double *ptr_v, double *ptr_a) {

  assert(std::abs(rs - s_condition[0]) < 1.0e-6);

  const double cos_theta_r = std::cos(rtheta);
  const double sin_theta_r = std::sin(rtheta);

  *ptr_x = rx - sin_theta_r * d_condition[0];
  *ptr_y = ry + cos_theta_r * d_condition[0];

  const double one_minus_kappa_r_d = 1 - rkappa * d_condition[0];

  const double tan_delta_theta = d_condition[1] / one_minus_kappa_r_d;
  const double delta_theta = std::atan2(d_condition[1], one_minus_kappa_r_d);
  const double cos_delta_theta = std::cos(delta_theta);

  *ptr_theta = MathUtils::NormalizeAngle(delta_theta + rtheta);

  const double kappa_r_d_prime =
      rdkappa * d_condition[0] + rkappa * d_condition[1];
  *ptr_kappa = (((d_condition[2] + kappa_r_d_prime * tan_delta_theta) *
      cos_delta_theta * cos_delta_theta) /
      (one_minus_kappa_r_d) +
      rkappa) *
      cos_delta_theta / (one_minus_kappa_r_d);

  const double d_dot = d_condition[1] * s_condition[1];
  *ptr_v = std::sqrt(one_minus_kappa_r_d * one_minus_kappa_r_d *
      s_condition[1] * s_condition[1] +
      d_dot * d_dot);

  const double delta_theta_prime =
      one_minus_kappa_r_d / cos_delta_theta * (*ptr_kappa) - rkappa;

  *ptr_a = s_condition[2] * one_minus_kappa_r_d / cos_delta_theta +
      s_condition[1] * s_condition[1] / cos_delta_theta *
          (d_condition[1] * delta_theta_prime - kappa_r_d_prime);
}

double CoordinateTransformer::CalcTheta(double rtheta, double rkappa,
                                        double l, double dl) {
  return MathUtils::NormalizeAngle(rtheta + std::atan2(dl, 1 - l * rkappa));
}

double CoordinateTransformer::CalcKappa(double rkappa, double rdkappa,
                                        double l, double dl, double ddl) {

  double denominator = (dl * dl + (1 - l * rkappa) * (1 - l * rkappa));
  if (std::fabs(denominator) < 1e-8) {
    return 0.0;
  }
  denominator = std::pow(denominator, 1.5);
  const double numerator = rkappa + ddl - 2 * l * rkappa * rkappa -
      l * ddl * rkappa + l * l * rkappa * rkappa * rkappa +
      l * dl * rdkappa + 2 * dl * dl * rkappa;
  return numerator / denominator;
}

Eigen::Vector2d CoordinateTransformer::CalcCatesianPoint(double rtheta, double rx,
                                                         double ry, double l) {
  const double x = rx - l * std::sin(rtheta);
  const double y = ry + l * std::cos(rtheta);

  return {x, y};
}

double CoordinateTransformer::CalcLateralDerivative(double rtheta, double theta,
                                                    double l, double rkappa) {

  return (1 - rkappa * l) * std::tan(theta - rtheta);

}
double CoordinateTransformer::CalcSecondOrderLateralDerivative(double rtheta, double theta,
                                                               double rkappa, double kappa,
                                                               double rdkappa, double l) {
  const double dl = CalcLateralDerivative(rtheta, theta, l, rkappa);
  const double theta_diff = theta - rtheta;
  const double cos_theta_diff = std::cos(theta_diff);
  const double res = -(rdkappa * l + rkappa * dl) * std::tan(theta - rtheta) +
      (1 - rkappa * l) / (cos_theta_diff * cos_theta_diff) *
          (kappa * (1 - rkappa * l) / cos_theta_diff - rkappa);
  if (std::isinf(res)) {
  }
  return res;
}

}