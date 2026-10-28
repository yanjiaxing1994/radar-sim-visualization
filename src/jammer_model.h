#pragma once

#include "jammer_strategy.h"
#include "types.h"

// 干扰模型外观：根据 JammerParams::strategy 分派到具体策略。
class JammerModel {
public:
    JammerEffect evaluate(const RadarParams &radar, const JammerParams &jammer,
                          double rcsM2, double targetRangeM, double jammerRangeM,
                          double baseRangeM) const;
};
