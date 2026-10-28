#pragma once

#include "esm_model.h"
#include "jammer_model.h"
#include "radar_model.h"
#include "types.h"

// 检测判决模块：综合雷达、干扰与 ESM 模型。
class DetectionEngine {
public:
    // 单雷达检测评估。
    SimOutput evaluateSingleRadar(const SimInput &input) const;

    // 场景评估入口。当前只有一个雷达节点，直接复用单雷达结果；
    // 后续多雷达组网时在此汇总各节点输出。
    SimOutput evaluateScene(const SimInput &input) const;

private:
    RadarModel m_radar;
    JammerModel m_jammer;
    EsmModel m_esm;
};
