#include "detection.h"

#include <cmath>

SimOutput DetectionEngine::evaluateSingleRadar(const SimInput &input) const
{
    SimOutput out;

    // 所有平台都在同一场景局部坐标系中，距离均相对雷达位置计算。
    const double targetDx = input.target.x - input.radar_position.x;
    const double targetDy = input.target.y - input.radar_position.y;
    out.R_target = std::hypot(targetDx, targetDy);

    out.R_jammer = out.R_target; // 旧调用未提供干扰机位置时按自卫干扰处理
    if (input.jammer_position_valid) {
        const double jammerDx =
            input.jammer_position.x - input.radar_position.x;
        const double jammerDy =
            input.jammer_position.y - input.radar_position.y;
        out.R_jammer = std::hypot(jammerDx, jammerDy);
    }

    out.R0 = m_radar.baseDetectionRange(input.radar, input.target.rcs);
    out.R_eff = out.R0;

    if (input.jammer_on) {
        const JammerEffect effect =
            m_jammer.evaluate(input.radar, input.jammer, input.target.rcs,
                              out.R_target, out.R_jammer, out.R0);
        out.js_ratio_db = effect.js_ratio_db;
        out.R_eff = effect.effective_range_m;
    }

    out.esm_detected = m_esm.canIntercept(input.esm_on, out.R0, out.R_target);
    out.target_detected = out.R_target < out.R_eff;

    if (input.jammer_on && !out.target_detected)
        out.status = "LOST";
    else if (input.jammer_on && out.target_detected)
        out.status = "JAMMED";
    else if (out.esm_detected)
        out.status = "ESM";
    else
        out.status = "NORMAL";

    return out;
}

SimOutput DetectionEngine::evaluateScene(const SimInput &input) const
{
    // 多雷达组网扩展点：当前场景仅有一个雷达节点。
    return evaluateSingleRadar(input);
}
