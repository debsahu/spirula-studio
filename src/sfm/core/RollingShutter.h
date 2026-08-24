// Rolling shutter: the per-image twist over the readout interval, the
// per-camera shutter geometry, and the `rolling_shutter.bin` sidecar.
//
// The twist is in the CAMERA frame, so it is Sim3-covariant exactly as Pose::t
// is: omega is invariant, v scales with the world.
// Shutter time s = ax*u + ay*v - 0.5 lies in [-0.5, 0.5] with s = 0 at the
// image centre, so the pose in images.bin is the mid-row pose -- the best
// global-shutter approximation of the model, not an endpoint of it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "sfm/core/Pose.h"
#include "sfm/geometry/LinAlg.h"

namespace sfm {

// Camera-frame motion over one readout interval: x -> exp(omega) x + v.
struct Twist {
    Vec3 omega;
    Vec3 v;

    bool isZero() const {
        return omega.norm() == 0.0 && v.norm() == 0.0;
    }
    Twist scaled(double s) const { return {omega * s, v * s}; }
};

// The sign of ShutterModel::readout carries the reverse directions, so
// bottom-to-top is Vertical with a negative readout.
enum class ShutterDir : uint8_t { Global = 0, Vertical = 1, Horizontal = 2 };

inline const char* shutterDirName(ShutterDir d) {
    switch (d) {
        case ShutterDir::Vertical:   return "vertical";
        case ShutterDir::Horizontal: return "horizontal";
        default:                     return "global";
    }
}

struct ShutterModel {
    ShutterDir dir = ShutterDir::Global;
    double readout = 0;  // in the run's timestamp unit; sign = direction
};

// s = ax*u + ay*v - 0.5, the one form both the host and ba.slang evaluate.
struct ShutterAxis {
    double ax = 0, ay = 0;

    static ShutterAxis of(ShutterDir d, int w, int h) {
        if (d == ShutterDir::Vertical && h > 1) return {0.0, 1.0 / h};
        if (d == ShutterDir::Horizontal && w > 1) return {1.0 / w, 0.0};
        return {0.0, 0.0};
    }
    double time(const Vec2& px) const { return ax * px.x + ay * px.y - 0.5; }
};

// x_cam(s) = exp(s*omega) (R X + t) + s*v.
inline Pose poseAtShutterTime(const Pose& ref, const Twist& xi, double s) {
    if (xi.isZero() || s == 0.0) return ref;
    Mat3 Rs = angleAxisToRotation(xi.omega * s);
    return {mul(Rs, ref.R), mul(Rs, ref.t) + xi.v * s};
}

// The twist for which poseAtShutterTime(a, ., 1) == b.
inline Twist twistBetween(const Pose& a, const Pose& b) {
    Mat3 Rd = mul(b.R, transpose(a.R));
    return {rotationToAngleAxis(Rd), b.t - mul(Rd, a.t)};
}

inline Twist transformTwist(const Sim3& T, const Twist& xi) {
    return {xi.omega, xi.v * T.scale};
}

// ---- timestamps ----------------------------------------------------------

enum class TimeUnit : uint8_t { Index = 0, Frames = 1, Seconds = 2 };

inline const char* timeUnitName(TimeUnit u) {
    switch (u) {
        case TimeUnit::Frames:  return "frames";
        case TimeUnit::Seconds: return "seconds";
        default:                return "index";
    }
}

// The last digit run of the file stem, which is where every frame extractor
// this pipeline sees puts the frame number. False if there is none.
inline bool frameNumberFromName(const std::string& name, double& out) {
    size_t slash = name.find_last_of("/\\");
    size_t b = (slash == std::string::npos) ? 0 : slash + 1;
    size_t dot = name.find_last_of('.');
    size_t e = (dot == std::string::npos || dot < b) ? name.size() : dot;

    size_t hi = e;
    while (hi > b && !isdigit((unsigned char)name[hi - 1])) hi--;
    if (hi == b) return false;
    size_t lo = hi;
    while (lo > b && isdigit((unsigned char)name[lo - 1])) lo--;
    if (hi - lo > 18) return false;

    out = std::strtod(name.substr(lo, hi - lo).c_str(), nullptr);
    return true;
}

// Frame numbers for `names`, or false if any name lacks one or two share one
// -- in which case the caller must fall back to the position in the sequence.
inline bool frameNumbers(const std::vector<std::string>& names, std::vector<double>& out) {
    out.assign(names.size(), 0.0);
    std::vector<double> seen;
    seen.reserve(names.size());
    for (size_t i = 0; i < names.size(); i++) {
        if (!frameNumberFromName(names[i], out[i])) return false;
        seen.push_back(out[i]);
    }
    std::sort(seen.begin(), seen.end());
    for (size_t i = 1; i < seen.size(); i++)
        if (seen[i] == seen[i - 1]) return false;
    return true;
}

// ---- sidecar -------------------------------------------------------------

struct RollingShutterData {
    struct ImageRS {
        Twist twist;
        double timestamp = 0;
    };
    TimeUnit unit = TimeUnit::Index;
    std::map<uint32_t, ShutterModel> cameras;
    std::map<uint32_t, ImageRS> images;

    bool active() const {
        for (const auto& kv : cameras)
            if (kv.second.dir != ShutterDir::Global && kv.second.readout != 0) return true;
        return false;
    }
};

static constexpr uint32_t kRollingShutterMagic = 0x53525353u;  // "SSRS"
static constexpr uint32_t kRollingShutterVersion = 1;

namespace detail {
template <class T>
inline void rsPut(std::ofstream& f, T v) {
    f.write((const char*)&v, sizeof(T));
}
template <class T>
inline T rsGet(std::ifstream& f) {
    T v{};
    f.read((char*)&v, sizeof(T));
    return v;
}
}  // namespace detail

inline void writeRollingShutter(const std::string& dir, const RollingShutterData& rs) {
    using namespace detail;
    {
        std::ofstream f(dir + "/rolling_shutter.bin", std::ios::binary);
        if (!f) throw std::runtime_error("cannot write rolling_shutter.bin in " + dir);
        rsPut<uint32_t>(f, kRollingShutterMagic);
        rsPut<uint32_t>(f, kRollingShutterVersion);
        rsPut<uint8_t>(f, (uint8_t)rs.unit);
        rsPut<uint64_t>(f, rs.cameras.size());
        for (const auto& kv : rs.cameras) {
            rsPut<uint32_t>(f, kv.first);
            rsPut<uint8_t>(f, (uint8_t)kv.second.dir);
            rsPut<double>(f, kv.second.readout);
        }
        rsPut<uint64_t>(f, rs.images.size());
        for (const auto& kv : rs.images) {
            rsPut<uint32_t>(f, kv.first);
            rsPut<double>(f, kv.second.timestamp);
            const Twist& x = kv.second.twist;
            for (double d : {x.omega.x, x.omega.y, x.omega.z, x.v.x, x.v.y, x.v.z})
                rsPut<double>(f, d);
        }
    }
    std::ofstream t(dir + "/rolling_shutter.txt");
    if (!t) return;
    t << "# Rolling shutter parameters, version " << kRollingShutterVersion << "\n";
    t << "# Shutter time s = ax*u + ay*v - 0.5; poses in images.bin are at s = 0.\n";
    t << "# time_unit " << timeUnitName(rs.unit) << "\n";
    t << "# CAMERA_ID, DIRECTION, READOUT\n";
    for (const auto& kv : rs.cameras)
        t << kv.first << " " << shutterDirName(kv.second.dir) << " " << kv.second.readout << "\n";
    t << "# IMAGE_ID, TIMESTAMP, WX, WY, WZ, VX, VY, VZ\n";
    for (const auto& kv : rs.images) {
        const Twist& x = kv.second.twist;
        t << kv.first << " " << kv.second.timestamp << " " << x.omega.x << " " << x.omega.y << " "
          << x.omega.z << " " << x.v.x << " " << x.v.y << " " << x.v.z << "\n";
    }
}

inline bool readRollingShutter(const std::string& dir, RollingShutterData& rs) {
    using namespace detail;
    std::ifstream f(dir + "/rolling_shutter.bin", std::ios::binary);
    if (!f) return false;
    if (rsGet<uint32_t>(f) != kRollingShutterMagic) return false;
    if (rsGet<uint32_t>(f) > kRollingShutterVersion) return false;
    rs.unit = (TimeUnit)rsGet<uint8_t>(f);
    uint64_t nc = rsGet<uint64_t>(f);
    for (uint64_t i = 0; i < nc && f; i++) {
        uint32_t id = rsGet<uint32_t>(f);
        ShutterModel m;
        m.dir = (ShutterDir)rsGet<uint8_t>(f);
        m.readout = rsGet<double>(f);
        rs.cameras[id] = m;
    }
    uint64_t ni = rsGet<uint64_t>(f);
    for (uint64_t i = 0; i < ni && f; i++) {
        uint32_t id = rsGet<uint32_t>(f);
        RollingShutterData::ImageRS r;
        r.timestamp = rsGet<double>(f);
        r.twist.omega = {rsGet<double>(f), rsGet<double>(f), rsGet<double>(f)};
        r.twist.v = {rsGet<double>(f), rsGet<double>(f), rsGet<double>(f)};
        rs.images[id] = r;
    }
    return (bool)f;
}

}  // namespace sfm
