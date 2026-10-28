#pragma once

#include <string>

// 统一单位：功率为瓦，波长为米，距离为米，增益为线性值，J/S 为 dB。
// 物理层纯 C++17：本文件及 radar_model / jammer_model / esm_model / detection
// 均不得包含 Qt 头文件，便于独立移植到 ARM64 / 银河麒麟。

struct RadarParams {
    double Pt     = 1e5;    // 发射功率 W
    double Gt     = 1000.0; // 天线增益（线性，对应 30 dB）
    double lambda = 0.03;   // 波长 m
    double Pmin   = 1e-13;  // 最小可检测功率 W
    double loss   = 4.0;    // 系统损耗（线性）
};

struct JammerParams {
    double Pj = 1e3;
    double Gj = 100.0;  // 增益（线性，对应 20 dB）
    double js_threshold_db = 10.0;
    // 干扰策略注册名，对应 IJammerStrategy 实现；默认噪声压制。
    std::string strategy = "noise";
};

// 可通过配置文件/API 调整的默认雷达与干扰参数。
struct SimConfig {
    RadarParams radar;
    JammerParams jammer;
};

// 场景局部平面坐标，单位米。x 向东、y 向北。
struct Position2D {
    double x = 0.0, y = 0.0;
};

struct TargetState {
    // 默认位于雷达原点东北方约 25 km，仍在 R0 之内。
    double x = 20000, y = 15000, rcs = 5.0;
};

struct SimInput {
    RadarParams radar;
    JammerParams jammer;
    TargetState target;

    // 雷达和干扰机的位置均是独立的场景局部坐标。
    // 目标距离由 target 与 radar_position 计算，不再假设雷达位于原点。
    Position2D radar_position;
    Position2D jammer_position;

    // 旧版 API 不传 jammer_position 时保持自卫干扰几何：
    // 干扰机与目标同距（R_j = R_t）。新前端总是显式传入该字段。
    bool jammer_position_valid = false;

    bool esm_on = false;
    bool jammer_on = false;
};

struct SimOutput {
    double R0 = 0, R_eff = 0, R_target = 0, R_jammer = 0, js_ratio_db = -999;
    bool esm_detected = false;
    bool target_detected = false;
    std::string status = "NORMAL";
};
