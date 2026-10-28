#include "jammer_model.h"

#include <cmath>
#include <map>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr const char *kDefaultStrategy = "noise";

// 噪声压制干扰（当前实现）。
class NoiseJamStrategy final : public IJammerStrategy {
public:
    std::string name() const override { return kDefaultStrategy; }

    JammerEffect evaluate(const RadarParams &radar, const JammerParams &jammer,
                          double rcsM2, double targetRangeM,
                          double jammerRangeM,
                          double baseRangeM) const override
    {
        JammerEffect effect;
        effect.effective_range_m = baseRangeM;
        if (baseRangeM <= 0.0)
            return effect;

        // 缺省时退化为自卫干扰：干扰机与目标同距。
        const double effectiveJammerRange =
            jammerRangeM > 0.0 ? jammerRangeM : targetRangeM;

        effect.js_ratio_db =
            jsRatioDb(radar, jammer, rcsM2, targetRangeM, effectiveJammerRange);
        if (effect.js_ratio_db > jammer.js_threshold_db) {
            const double excess = effect.js_ratio_db - jammer.js_threshold_db;
            effect.effective_range_m =
                baseRangeM / std::pow(1.0 + excess / 10.0, 0.25);
        }

        return effect;
    }

private:
    // 支援干扰几何：
    // J/S = (Pj * Gj * 4*pi * R_t^4) / (Pt * Gt * sigma * R_j^2)
    // 当 R_j = R_t 时退化为自卫干扰。返回 dB。
    static double jsRatioDb(const RadarParams &radar, const JammerParams &jammer,
                            double rcsM2, double targetRangeM,
                            double jammerRangeM)
    {
        if (radar.Pt <= 0.0 || radar.Gt <= 0.0 || jammer.Pj <= 0.0
            || jammer.Gj <= 0.0 || rcsM2 <= 0.0 || targetRangeM <= 0.0
            || jammerRangeM <= 0.0) {
            return -999.0;
        }

        const double rt2 = targetRangeM * targetRangeM;
        const double js = (jammer.Pj * jammer.Gj * 4.0 * kPi * rt2 * rt2)
                        / (radar.Pt * radar.Gt * rcsM2
                           * jammerRangeM * jammerRangeM);
        if (js <= 0.0)
            return -999.0;

        return 10.0 * std::log10(js);
    }
};

// 策略注册表：后续 DeceptionJamStrategy、RangeGatePullOffStrategy
// 在此登记即可，调用方保持不动。
const std::map<std::string, const IJammerStrategy *> &strategyRegistry()
{
    static const NoiseJamStrategy noise;
    static const std::map<std::string, const IJammerStrategy *> registry{
        {noise.name(), &noise},
    };
    return registry;
}

} // namespace

const IJammerStrategy *findJammerStrategy(const std::string &name)
{
    const auto &registry = strategyRegistry();
    const auto it = registry.find(name);
    return it == registry.end() ? nullptr : it->second;
}

JammerEffect JammerModel::evaluate(const RadarParams &radar,
                                   const JammerParams &jammer,
                                   double rcsM2, double targetRangeM,
                                   double jammerRangeM,
                                   double baseRangeM) const
{
    const IJammerStrategy *strategy = findJammerStrategy(jammer.strategy);
    if (!strategy)
        strategy = findJammerStrategy(kDefaultStrategy);

    if (!strategy) {
        JammerEffect fallback;
        fallback.effective_range_m = baseRangeM;
        return fallback;
    }

    return strategy->evaluate(radar, jammer, rcsM2, targetRangeM,
                              jammerRangeM, baseRangeM);
}
