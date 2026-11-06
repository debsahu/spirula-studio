// pano360 -- which files are 360 packings, and the views cut from a stitched
// panorama (app/Pano360.h). Each block names the wrong implementation it
// fails on.

#include "app/Pano360.h"
#include "app/Pano360Rig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;

void expect(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "BAD ", what.c_str());
    if (!ok) g_failures++;
}

app::Pano360Meta tagged(const char* spherical) {
    app::Pano360Meta m;
    m.spherical = spherical;
    return m;
}

// Catches: a shape-only rule (the untagged 2:1 files), a tag-only rule (the
// 16:9 one), accepting a cropped or cubemap tag, and any change to the GoPro
// branches.
void detection() {
    app::Pano360Layout l;
    expect(app::pano360_detect(1, 7680, 3840, tagged("equirectangular"), l) &&
               l.sphere() && l.margin == 0 && l.face == 1920 && l.canvasW() == 7680 &&
               l.canvasH() == 3840 && !app::pano360_needs_track1(l),
           "tagged 7680x3840 single track is a stitched panorama");
    expect(app::pano360_detect(1, 5760, 2880, tagged("equirectangular"), l) &&
               l.face == 1440,
           "tagged 5760x2880 single track is a stitched panorama");
    expect(!app::pano360_detect(1, 2048, 1024, app::Pano360Meta{}, l) && !l.valid(),
           "untagged 2048x1024 (dual fisheye proxy) is not a panorama");
    expect(!app::pano360_detect(1, 1664, 832, app::Pano360Meta{}, l),
           "untagged 1664x832 (.lrv) is not a panorama");
    expect(!app::pano360_unsupported(1, 2048, 1024, app::Pano360Meta{}),
           "untagged 2:1 is not reported as an unsupported 360 packing");
    expect(!app::pano360_detect(1, 1920, 1080, tagged("equirectangular"), l),
           "tagged 1920x1080 is not 2:1 and is refused");
    expect(!app::pano360_detect(1, 7682, 3841, tagged("equirectangular"), l),
           "tagged width not a multiple of 4 is refused");
    expect(!app::pano360_detect(1, 7680, 3840, tagged("equirectangular-cropped"), l) &&
               app::pano360_unsupported(1, 7680, 3840, tagged("equirectangular-cropped")),
           "a cropped panorama is an unsupported 360 packing");
    expect(!app::pano360_detect(1, 6144, 4096, tagged("cubemap"), l) &&
               app::pano360_unsupported(1, 6144, 4096, tagged("cubemap")),
           "a tagged cubemap is an unsupported 360 packing");
    expect(!app::pano360_detect(2, 7680, 3840, tagged("equirectangular"), l),
           "two tagged tracks are not a single panorama");
    expect(!app::pano360_detect(2, 3840, 3840, app::Pano360Meta{}, l),
           "two square fisheye tracks (.osv) are not a 360 packing");

    expect(app::pano360_detect(2, 4096, 1344, app::Pano360Meta{}, l) && !l.sphere() &&
               l.face == 1344 && l.strip == 32,
           "MAX 5.6K EAC unchanged");
    expect(app::pano360_detect(2, 4096, 1344, tagged("equirectangular"), l) && !l.sphere(),
           "a spherical tag does not reroute a MAX");
    app::Pano360Meta max2;
    max2.projection = "EACO";
    max2.mode = {48, 2016, 0, 0};
    expect(app::pano360_detect(2, 5952, 1920, max2, l) && l.sphere() && l.margin == 1008 &&
               l.face == 984,
           "MAX 2 panorama pair unchanged");

    expect(app::pano360_equirect_layout(2048, 1024, l) && l.sphere() && l.face == 512,
           "the caller can force an untagged 2:1 track to equirect");
    expect(!app::pano360_equirect_layout(2048, 1536, l), "forcing still checks 2:1");
}

double deg(double r) { return r * 180.0 / kPi; }

// Catches: an axis-aligned cube (elevation 45 or 0/90), a 90-degree face,
// faces not on a cube (non-orthogonal axes), and a rolled face.
void corner_cube() {
    app::Pano360Layout l;
    app::pano360_detect(1, 7680, 3840, tagged("equirectangular"), l);
    app::Pano360Options o;
    o.mode = app::Pano360Mode::Faces;
    const std::vector<app::Pano360View> v = app::pano360_views(l, o);
    expect(v.size() == 6, "six faces");
    if (v.size() != 6) return;
    expect(app::pano360_default_size(l, o) == 2176 && v[0].width == 2176 &&
               v[0].height == 2176,
           "default face 2176 px at 7680 (1.125x the quarter, on 32)");
    bool fov = true, upright = true, elev = true, dirs = true;
    double az_up[3], az_down[3];
    int nu = 0, nd = 0;
    for (size_t k = 0; k < v.size(); k++) {
        const float* r = v[k].rot;
        fov = fov && v[k].fov == 100.0f && v[k].dir == "cam" + std::to_string(k);
        // Column 0 is the view's right in the camera frame (y down): level.
        upright = upright && std::fabs(r[3]) < 1e-6 && r[4] > 0.5f;
        const double ez[3] = {r[2], r[5], r[8]};
        const double el = deg(std::asin(-ez[1]));   // positive up
        elev = elev && std::fabs(std::fabs(el) - 35.2644) < 0.01;
        const double az = deg(std::atan2(ez[0], ez[2]));
        if (el > 0 && nu < 3) az_up[nu++] = az;
        if (el < 0 && nd < 3) az_down[nd++] = az;
        for (size_t j = 0; j < k; j++) {
            const float* q = v[j].rot;
            const double d = ez[0] * q[2] + ez[1] * q[5] + ez[2] * q[8];
            dirs = dirs && (std::fabs(d) < 1e-5 || std::fabs(d + 1.0) < 1e-5);
        }
    }
    expect(fov, "every face is 100 degrees, cam0..cam5");
    expect(upright, "every face keeps the panorama's up");
    expect(elev, "face centres at +-35.26 degrees of elevation");
    expect(dirs, "face axes are the +- of three orthogonal axes");
    auto spaced = [](double* a, int n) {
        if (n != 3) return false;
        std::sort(a, a + 3);
        return std::fabs(a[1] - a[0] - 120.0) < 0.01 && std::fabs(a[2] - a[1] - 120.0) < 0.01;
    };
    expect(spaced(az_up, nu) && spaced(az_down, nd),
           "three up and three down, 120 degrees apart in azimuth");
    expect(std::fabs(app::pano360_focal_factor(v[0]) - 0.5 / std::tan(50.0 * kPi / 180.0)) <
               1e-9,
           "focal factor of a 100-degree face is 0.5/tan(50)");
}

// A smooth colour per direction, distinct in every component, so a mirrored,
// shifted or rotated lookup lands on a different colour.
void colour(const double d[3], double c[3]) {
    for (int i = 0; i < 3; i++) c[i] = 127.5 + 100.0 * d[i];
}

// Canvas -> faces -> canvas through pano360_direction, the inverse the layout
// itself states. Catches a wrong azimuth origin or sign, a wrong elevation
// offset, or a face rotation that disagrees with its `rot`.
void round_trip() {
    const int W = 1024, H = 512;
    app::Pano360Layout l;
    app::pano360_detect(1, W, H, tagged("equirectangular"), l);
    std::vector<uint8_t> canvas((size_t)W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float d[3];
            app::pano360_direction(l, 0, x + 0.5f, y + 0.5f, d);
            const double dd[3] = {d[0], d[1], d[2]};
            double c[3];
            colour(dd, c);
            for (int i = 0; i < 3; i++)
                canvas[((size_t)y * W + x) * 3 + i] = (uint8_t)std::lround(c[i]);
        }

    app::Pano360Options o;
    o.mode = app::Pano360Mode::Faces;
    o.size = 256;
    const std::vector<app::Pano360View> views = app::pano360_views(l, o);
    expect(views.size() == 6, "round trip: six faces to sample");
    if (views.size() != 6) return;
    std::vector<std::vector<uint8_t>> faces(views.size());
    double face_err = 0;
    long checked = 0;
    for (size_t k = 0; k < views.size(); k++) {
        app::Pano360Remap m;
        app::pano360_remap(l, views[k], m);
        faces[k].resize((size_t)m.width * m.height * 3);
        app::pano360_apply(m, canvas.data(), l.canvasW(), l.canvasH(), 1, faces[k].data());
        // Each face pixel against the colour of its own ray, from `rot` and `fov`.
        const double f = views[k].width * app::pano360_focal_factor(views[k]);
        const float* r = views[k].rot;
        for (int j = 0; j < m.height; j += 7)
            for (int i = 0; i < m.width; i += 7) {
                const double vx = i + 0.5 - m.width / 2.0, vy = j + 0.5 - m.height / 2.0;
                double d[3];
                for (int a = 0; a < 3; a++) d[a] = r[3 * a] * vx + r[3 * a + 1] * vy + r[3 * a + 2] * f;
                const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                for (double& x : d) x /= n;
                double c[3];
                colour(d, c);
                checked++;
                for (int a = 0; a < 3; a++)
                    face_err = std::max(face_err,
                                        std::fabs(c[a] - faces[k][((size_t)j * m.width + i) * 3 + a]));
            }
    }
    expect(checked > 6 * 1000, "round trip: face pixels were checked");
    expect(face_err < 3.0, "every face pixel holds its own ray's colour (max err " +
                               std::to_string(face_err) + ")");

    // Back: each canvas pixel from the face whose axis it is nearest.
    double back_err = 0;
    int missed = 0;
    long back_checked = 0;
    for (int y = 8; y < H - 8; y += 3)
        for (int x = 0; x < W; x += 3) {
            float d[3];
            app::pano360_direction(l, 0, x + 0.5f, y + 0.5f, d);
            // The nearest axis's face first; a level face is rolled off the
            // cube's edges, so its corner can fall to a neighbour.
            int best = -1, ix = 0, iy = 0;
            double best_z = 0;
            for (size_t k = 0; k < views.size(); k++) {
                const float* r = views[k].rot;
                const double z = r[2] * d[0] + r[5] * d[1] + r[8] * d[2];
                if (z <= 0.1) continue;
                const double vx = r[0] * d[0] + r[3] * d[1] + r[6] * d[2];
                const double vy = r[1] * d[0] + r[4] * d[1] + r[7] * d[2];
                const double f = views[k].width * app::pano360_focal_factor(views[k]);
                const int cx = (int)std::lround(vx / z * f + views[k].width / 2.0 - 0.5);
                const int cy = (int)std::lround(vy / z * f + views[k].height / 2.0 - 0.5);
                if (cx < 1 || cy < 1 || cx >= views[k].width - 1 || cy >= views[k].height - 1)
                    continue;
                if (z > best_z) { best_z = z; best = (int)k; ix = cx; iy = cy; }
            }
            if (best < 0) {
                missed++;
                continue;
            }
            back_checked++;
            for (int a = 0; a < 3; a++)
                back_err = std::max(
                    back_err,
                    std::fabs((double)canvas[((size_t)y * W + x) * 3 + a] -
                              faces[best][((size_t)iy * views[best].width + ix) * 3 + a]));
        }
    // Level faces are rolled off the cube's edges, so at 100 degrees wedges
    // between them reach no face: 2.26% of the sphere by area, measured
    // separately; full cover takes 109.5. Pinned so a plan change shows here.
    const double gap = (double)missed / (double)(missed + back_checked);
    expect(back_checked > 50000 && gap > 0.02 && gap < 0.08,
           "the 100-degree corner cube leaves its measured gap (" +
               std::to_string(100.0 * gap) + "% of samples)");
    expect(back_err < 4.0, "canvas -> faces -> canvas (max err " + std::to_string(back_err) + ")");

    // --360 equirect at the source size is the source.
    app::Pano360Options e;
    e.mode = app::Pano360Mode::Equirect;
    const std::vector<app::Pano360View> pass = app::pano360_views(l, e);
    bool same = pass.size() == 1 && pass[0].dir.empty() && pass[0].width == W &&
                pass[0].height == H;
    if (same) {
        app::Pano360Remap m;
        app::pano360_remap(l, pass[0], m);
        std::vector<uint8_t> out((size_t)W * H * 3);
        app::pano360_apply(m, canvas.data(), W, H, 1, out.data());
        int worst = 0;
        for (size_t i = 0; i < out.size(); i++)
            worst = std::max(worst, std::abs((int)out[i] - (int)canvas[i]));
        same = worst <= 1;
    }
    expect(same, "--360 equirect passes the panorama through");
}

// Catches: the rotation handed over untransposed, a face left out of the rig,
// and translation left free on views that share one centre.
void rig() {
    app::Pano360Layout l;
    app::pano360_detect(1, 7680, 3840, tagged("equirectangular"), l);
    app::Pano360Options o;
    o.mode = app::Pano360Mode::Faces;
    const std::vector<app::Pano360View> views = app::pano360_views(l, o);
    sfm::RigDef d;
    for (const app::Pano360View& v : views) d.members.push_back({v.dir});
    expect(app::pano360_rig_members(l, views, d), "six members accepted");
    bool ok = d.members.size() == 6;
    for (size_t m = 0; ok && m < d.members.size(); m++) {
        const sfm::RigMemberDef& md = d.members[m];
        ok = md.has_ext && !md.ext_fixed && md.dof == sfm::kRigDofRotation;
        // The face's own axis, in the panorama frame, is +z in that camera.
        const float* r = views[m].rot;
        const double ez[3] = {r[2], r[5], r[8]};
        double z[3];
        for (int a = 0; a < 3; a++)
            z[a] = md.ext.R[3 * a] * ez[0] + md.ext.R[3 * a + 1] * ez[1] + md.ext.R[3 * a + 2] * ez[2];
        ok = ok && std::fabs(z[0]) < 1e-6 && std::fabs(z[1]) < 1e-6 && std::fabs(z[2] - 1) < 1e-6;
    }
    expect(ok, "each member is cam_from_rig of its face, rotation refined only");
    sfm::RigDef five;
    for (int m = 0; m < 5; m++) five.members.push_back({"cam" + std::to_string(m)});
    expect(!app::pano360_rig_members(l, views, five), "a rig missing a face is refused");
}

}  // namespace

int main() {
    detection();
    corner_cube();
    round_trip();
    rig();
    if (g_failures == 0) std::printf("pano360_test: OK\n");
    return g_failures == 0 ? 0 : 1;
}
