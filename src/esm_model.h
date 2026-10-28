#pragma once

#include "types.h"

// ESM（电子支援措施）侦察截获模型。
class EsmModel {
public:
    // 截获距离 = 1.2 * R0
    double interceptionRangeM(double baseRangeM) const;

    // esm_on && R_target < R0 * 1.2 时截获成功。
    bool canIntercept(bool esmOn, double baseRangeM, double targetRangeM) const;
};
