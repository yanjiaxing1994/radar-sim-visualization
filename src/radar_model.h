#pragma once

#include "types.h"

// 雷达方程模型。
class RadarModel {
public:
    // 自由空间基准探测距离：
    // R0 = (Pt * Gt^2 * lambda^2 * sigma /
    //       ((4*pi)^3 * Pmin * loss))^(1/4)，单位米。
    double computeR0(const RadarParams &radar, double rcsM2) const;

    // 大气衰减扩展点：返回给定距离处的双程线性功率损耗（>= 1）。
    // 当前模型不引入额外衰减，恒为 1.0；替换模型时只改此函数。
    double computeAtmosphericLoss(double rangeM) const;

    // 综合自由空间 R0 与距离相关的大气损耗，求自洽的有效探测距离。
    double baseDetectionRange(const RadarParams &radar, double rcsM2) const;
};
