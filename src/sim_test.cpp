#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "detection.h"
#include "types.h"

namespace {

std::string fmt(double value, int precision = 3)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

void runCase(const std::string &name, const SimInput &input)
{
    const DetectionEngine engine;
    const SimOutput out = engine.evaluateScene(input);

    std::cout << name << '\n'
              << "  R0=" << fmt(out.R0, 1) << " m"
              << "  R_eff=" << fmt(out.R_eff, 1) << " m"
              << "  R_target=" << fmt(out.R_target, 1) << " m"
              << "  R_jammer=" << fmt(out.R_jammer, 1) << " m"
              << "  J/S="
              << (out.js_ratio_db <= -900.0 ? std::string("--")
                                            : fmt(out.js_ratio_db, 2) + " dB")
              << '\n'
              << "  esm_detected=" << (out.esm_detected ? "true" : "false")
              << "  target_detected=" << (out.target_detected ? "true" : "false")
              << "  status=" << out.status << "\n\n";
}

} // namespace

int main()
{
    SimInput base; // 默认参数：目标位于 (20000, 15000) m

    SimInput farTarget = base;
    farTarget.target = {80000.0, 60000.0, 5.0};

    runCase("1) 默认参数：无干扰、无 ESM、目标 25 km", base);

    SimInput closeTarget = base;
    closeTarget.target = {2000.0, 1500.0, 5.0};
    runCase("2) 近距离目标 2.5 km，无干扰", closeTarget);

    SimInput closeJammed = closeTarget;
    closeJammed.jammer_on = true;
    runCase("3) 近距离目标 2.5 km + 自卫干扰", closeJammed);

    SimInput farJammed = farTarget;
    farJammed.jammer_on = true;
    runCase("4) 远距离目标 100 km + 自卫干扰", farJammed);

    SimInput esmOnly = base;
    esmOnly.esm_on = true;
    runCase("5) 默认目标 25 km + ESM 开机", esmOnly);

    SimInput esmFar = farTarget;
    esmFar.esm_on = true;
    runCase("6) 远距离目标 100 km + ESM 开机", esmFar);

    SimInput esmBeyond = base;
    esmBeyond.target = {30000.0, 0.0, 5.0};
    esmBeyond.esm_on = true;
    runCase("7) 目标 30 km + ESM 开机（雷达未探测，ESM 截获）", esmBeyond);

    // 雷达离开原点后，目标距离仍按两者相对位置计算。
    SimInput offsetRadar = base;
    offsetRadar.radar_position = {10000.0, 0.0};
    offsetRadar.target = {30000.0, 15000.0, 5.0};
    runCase("8) 雷达在 (10,0) km，目标仍距雷达 25 km", offsetRadar);

    // 支援干扰：干扰机不在目标处，J/S 随 R_j 增大而下降。
    SimInput standoff = farTarget;
    standoff.jammer_on = true;
    standoff.radar_position = {10000.0, 0.0};
    standoff.target = {90000.0, 60000.0, 5.0};
    standoff.jammer_position = {210000.0, 0.0};
    standoff.jammer_position_valid = true;
    runCase("9) 100 km 目标 + 200 km 远距支援干扰（J/S 低于自卫干扰）",
            standoff);

    SimInput selfProtect = standoff;
    selfProtect.jammer_position = {selfProtect.target.x, selfProtect.target.y};
    runCase("10) 同一目标改为自卫干扰（R_j = R_t）", selfProtect);

    return 0;
}
