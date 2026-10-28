#include "radar_model.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxLossIterations = 8;
constexpr double kRangeToleranceM = 0.01;
}

double RadarModel::computeR0(const RadarParams &radar, double rcsM2) const
{
    if (radar.Pt <= 0.0 || radar.Gt <= 0.0 || radar.lambda <= 0.0
        || radar.Pmin <= 0.0 || radar.loss <= 0.0 || rcsM2 <= 0.0) {
        return 0.0;
    }

    const double numerator = radar.Pt * radar.Gt * radar.Gt
                           * radar.lambda * radar.lambda * rcsM2;
    const double denominator = std::pow(4.0 * kPi, 3) * radar.Pmin * radar.loss;

    return std::pow(numerator / denominator, 0.25);
}

double RadarModel::computeAtmosphericLoss(double) const
{
    // 扩展点：当前无额外大气衰减。返回双程线性功率损耗，1.0 表示 0 dB。
    // 后续可替换为随距离（以及频率、天气等）变化的衰减模型。
    return 1.0;
}

double RadarModel::baseDetectionRange(const RadarParams &radar, double rcsM2) const
{
    const double r0 = computeR0(radar, rcsM2);
    if (r0 <= 0.0)
        return 0.0;

    // 双程损耗 L(R) 使有效距离满足 R = R0 / L(R)^(1/4)。
    // 默认 L(R) == 1.0，一次迭代即得到 R0；替换衰减模型后自动迭代求解。
    double range = r0;
    for (int i = 0; i < kMaxLossIterations; ++i) {
        const double loss = computeAtmosphericLoss(range);
        if (!std::isfinite(loss) || loss <= 0.0)
            return 0.0;

        const double next = r0 / std::pow(loss, 0.25);
        if (!std::isfinite(next) || next <= 0.0)
            return 0.0;

        if (std::fabs(next - range) < kRangeToleranceM)
            return next;
        range = next;
    }

    return range;
}
