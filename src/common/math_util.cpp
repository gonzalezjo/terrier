#include "common/math_util.h"

namespace terrier::common {

bool MathUtil::ApproxEqual(float left, float right) {
  const double epsilon = std::fabs(right) * 0.01;
  return std::fabs(left - right) <= epsilon;
}

bool MathUtil::ApproxEqual(double left, double right) {
  const double epsilon = std::fabs(right) * 0.01;
  return std::fabs(left - right) <= epsilon;
}

}  // namespace terrier::common
