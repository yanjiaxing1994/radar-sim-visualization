#include "esm_model.h"

namespace {
constexpr double kInterceptRangeFactor = 1.2;
}

double EsmModel::interceptionRangeM(double baseRangeM) const
{
    if (baseRangeM <= 0.0)
        return 0.0;
    return baseRangeM * kInterceptRangeFactor;
}

bool EsmModel::canIntercept(bool esmOn, double baseRangeM, double targetRangeM) const
{
    if (!esmOn || baseRangeM <= 0.0)
        return false;
    return targetRangeM < interceptionRangeM(baseRangeM);
}
