#pragma once

#include <string>

#include "types.h"

// 一次干扰评估的结果。纯数据结构，不依赖 Qt。
struct JammerEffect {
    double js_ratio_db = -999.0;       // 不适用时保持 -999
    double effective_range_m = 0.0;
};

// 干扰策略接口。新增干扰样式时实现此接口并注册即可，
// DetectionEngine / HTTP 层等调用方无需修改。
class IJammerStrategy {
public:
    virtual ~IJammerStrategy() = default;

    virtual std::string name() const = 0;

    // targetRangeM 为雷达至目标的距离，jammerRangeM 为雷达至干扰机的距离。
    // baseRangeM 为无干扰时的基准探测距离。
    // 实现应处理非法输入，并在无法评估时保持 effective_range_m = baseRangeM。
    virtual JammerEffect evaluate(const RadarParams &radar,
                                  const JammerParams &jammer,
                                  double rcsM2,
                                  double targetRangeM,
                                  double jammerRangeM,
                                  double baseRangeM) const = 0;
};

// 按 JammerParams::strategy 查找已注册策略；不存在时返回 nullptr。
const IJammerStrategy *findJammerStrategy(const std::string &name);
