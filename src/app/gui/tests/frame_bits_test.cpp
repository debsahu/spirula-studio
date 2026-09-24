// frame_bits_test -- 16-bit frames out of a two-track video (DatasetPrep.h,
// PrepJob::frame_bits), through the real DatasetPrep::run and a real ffmpeg on
// a lossless 10-bit fixture the test generates. No ffmpeg on PATH is a SKIP.

#include "app/gui/DatasetPrep.h"
#include "app/gui/Subprocess.h"
#include "external/stb_image.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

fs::path g_root;

fs::path scratch(const char* name) {
    const fs::path d = g_root / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

// Two FFV1 tracks, 878x64, `frames` at 30 fps, Cb = Cr = 512: row 0 a luma ramp
// 64..940 (track 1 reversed), row 1 64 + N (no repeats for FrameSelect to drop),
// then a checkerboard on even frames and flat grey on odd ones, the blurrier.
bool make_fixture(const fs::path& out, int frames) {
    auto lum = [](const char* ramp) {
        const std::string row0 = ramp;
        return "if(eq(Y\\,0)\\," + row0 +
               "\\,if(eq(Y\\,1)\\,64+N\\,if(mod(N\\,2)\\,502\\,if(mod(X+Y\\,2)\\,940\\,64))))";
    };
    auto src = [&](const char* ramp) {
        return "nullsrc=s=878x64:d=" + std::to_string((frames + 1) / 30.0) +
               ":r=30,trim=end_frame=" + std::to_string(frames) +
               ",format=yuv420p10le,geq=lum='" + lum(ramp) +
               "':cb=512:cr=512";
    };
    const std::vector<std::string> argv{
        "ffmpeg", "-nostdin", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", src("64+min(X\\,876)"),
        "-f", "lavfi", "-i", src("940-min(X\\,876)"),
        "-map", "0:v", "-map", "1:v", "-c:v", "ffv1", "-level", "3",
        "-f", "matroska", out.string()};
    std::atomic<bool> cancel{false};
    const int rc = gui::run_process(argv, "", [](const std::string& l) {
        std::printf("      ffmpeg: %s\n", l.c_str());
    }, cancel);
    std::error_code ec;
    return rc == 0 && fs::file_size(out, ec) > 0;
}

sfm::VideoColor as_dlogm(const std::string&) {
    sfm::VideoColor c;
    c.mode = sfm::VideoColorMode::DlogM;
    c.code = 19;
    return c;
}

gui::PrepJob job_for(const fs::path& clip, const fs::path& ws, int bits, float fps,
                     int window) {
    gui::PrepJob job;
    job.workspace = ws.string();
    job.force_external_decode = true;
    job.frame_bits = bits;
    job.video_fps = fps;
    job.sharp_window = window;
    gui::PrepInput in;
    in.path = clip.string();
    in.is_video = true;
    job.inputs = {in};
    return job;
}

bool run_prep(const gui::PrepJob& job, gui::PrepResult& out, std::string& err) {
    gui::RunProgress prog;
    std::atomic<bool> cancel{false};
    gui::DatasetPrep prep(&prog, gui::RunFilms{}, cancel);
    const bool ok = prep.run(job, out, err);
    if (!ok)
        for (const gui::RunLine& l : prog.drain())
            std::printf("      log: %s\n", l.text.c_str());
    return ok;
}

std::vector<std::string> names_in(const fs::path& dir) {
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

bool any_jpg_under(const fs::path& dir) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".jpg") return true;
    return false;
}

bool is16(const fs::path& p) { return stbi_is_16_bit(p.string().c_str()) != 0; }

// One row as 16-bit RGB triples; empty when the file does not decode.
std::vector<stbi_us> row_rgb(const fs::path& p, int row) {
    int w = 0, h = 0, c = 0;
    stbi_us* px = stbi_load_16(p.string().c_str(), &w, &h, &c, 3);
    std::vector<stbi_us> v;
    if (px && row < h) v.assign(px + (size_t)row * w * 3, px + (size_t)(row + 1) * w * 3);
    if (px) stbi_image_free(px);
    return v;
}

std::vector<int> green(const std::vector<stbi_us>& rgb) {
    std::vector<int> g;
    for (size_t i = 1; i < rgb.size(); i += 3) g.push_back(rgb[i]);
    return g;
}

bool grey(const std::vector<stbi_us>& rgb) {
    for (size_t i = 0; i + 2 < rgb.size(); i += 3)
        if (rgb[i] != rgb[i + 1] || rgb[i + 1] != rgb[i + 2]) return false;
    return !rgb.empty();
}

int distinct(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return (int)(std::unique(v.begin(), v.end()) - v.begin());
}

bool within(int v, int want, int tol) { return std::abs(v - want) <= tol; }

// ---------------------------------------------------------------------------
// T1: every frame
// ---------------------------------------------------------------------------

void test_every_frame_16(const fs::path& clip) {
    const fs::path ws = scratch("t1a");
    gui::PrepResult out;
    std::string err;
    check(run_prep(job_for(clip, ws, 16, 0.0f, 1), out, err),
          "T1a: forced 16-bit prep runs: " + err);
    const fs::path c0 = ws / "images" / "cam0", c1 = ws / "images" / "cam1";
    check(fs::exists(c0 / "00000.png") && fs::exists(c1 / "00000.png"),
          "T1a: cam0/00000.png and cam1/00000.png exist");
    check(is16(c0 / "00000.png") && is16(c1 / "00000.png"), "T1a: both are 16-bit PNGs");
    check(names_in(c0).size() == 6 && names_in(c1).size() == 6, "T1a: 6 frames per folder");
    check(!any_jpg_under(ws / "images"), "T1a: no .jpg under images/");

    const std::vector<stbi_us> rgb = row_rgb(c0 / "00000.png", 0);
    const std::vector<int> g = green(rgb);
    check(g.size() == 878, "T1b: row 0 is 878 px");
    if (g.size() != 878) return;
    check(grey(rgb), "T1b: row 0 has R = G = B");
    check(std::is_sorted(g.begin(), g.end()), "T1b: row 0 does not decrease");
    const int n = distinct(g);
    check(n == 877, "T1b: 877 distinct codes on row 0 (got " + std::to_string(n) + ")");
    check(g[0] == 0, "T1b: Y=64 -> 0 (got " + std::to_string(g[0]) + ")");
    check(within(g[438], 32768, 1), "T1b: Y=502 -> 32768 (got " + std::to_string(g[438]) + ")");
    check(within(g[876], 65520, 1), "T1b: Y=940 -> 65520 (got " + std::to_string(g[876]) + ")");

    const std::vector<int> r = green(row_rgb(c1 / "00000.png", 0));
    check(r.size() == 878 && within(r[0], 65520, 1) && r[876] == 0 &&
              std::is_sorted(r.rbegin(), r.rend()),
          "T1c: cam1 row 0 is the reversed ramp, 65520 down to 0");
}

void test_auto_and_forced_8(const fs::path& clip) {
    {
        const fs::path ws = scratch("t1d");
        gui::PrepResult out;
        std::string err;
        check(run_prep(job_for(clip, ws, 0, 0.0f, 1), out, err), "T1d: auto prep runs: " + err);
        const fs::path f = ws / "images" / "cam0" / "00000.jpg";
        check(fs::exists(f) && !is16(f), "T1d: auto on an unrecorded clip writes 8-bit JPEG");
    }
    {
        const fs::path ws = scratch("t1e");
        gui::PrepJob job = job_for(clip, ws, 0, 0.0f, 1);
        job.read_color = as_dlogm;
        gui::PrepResult out;
        std::string err;
        check(run_prep(job, out, err), "T1e: auto prep of a D-Log M clip runs: " + err);
        const fs::path f = ws / "images" / "cam0" / "00000.png";
        check(fs::exists(f) && is16(f) && !any_jpg_under(ws / "images"),
              "T1e: auto on a D-Log M clip writes 16-bit PNG");
    }
    {
        const fs::path ws = scratch("t1f");
        gui::PrepJob job = job_for(clip, ws, 8, 0.0f, 1);
        job.read_color = as_dlogm;
        gui::PrepResult out;
        std::string err;
        check(run_prep(job, out, err), "T1f: forced 8-bit prep runs: " + err);
        check(fs::exists(ws / "images" / "cam0" / "00000.jpg") &&
                  !fs::exists(ws / "images" / "cam0" / "00000.png"),
              "T1f: forced 8 on a D-Log M clip writes JPEG");
    }
}

// ---------------------------------------------------------------------------
// T2: selection on JPEG candidates, then a second pass for the keepers
// ---------------------------------------------------------------------------

void test_selected_16(const fs::path& clip) {
    const fs::path ws = scratch("t2");
    gui::PrepResult out;
    std::string err;
    check(run_prep(job_for(clip, ws, 16, 15.0f, 2), out, err),
          "T2: selected 16-bit prep runs: " + err);
    const std::vector<std::string> want{"00000.png", "00002.png", "00004.png"};
    const fs::path c0 = ws / "images" / "cam0", c1 = ws / "images" / "cam1";
    const std::vector<std::string> got0 = names_in(c0), got1 = names_in(c1);
    std::string listed;
    for (const std::string& s : got0) listed += " " + s;
    check(got0 == want, "T2a: cam0 holds exactly 00000 00002 00004 (got" + listed + ")");
    for (int k : {0, 2, 4}) {
        char name[16];
        std::snprintf(name, sizeof name, "%05d.png", k);
        const std::vector<int> g = green(row_rgb(c0 / name, 1));
        const int want_g = (int)(k * 74.8 + 0.5);
        check(!g.empty() && within(g[0], want_g, 40),
              std::string("T2a: ") + name + " is source frame " + std::to_string(k) +
                  " (row 1 G " + (g.empty() ? std::string("-") : std::to_string(g[0])) +
                  ", want " + std::to_string(want_g) + ")");
        check(is16(c0 / name), std::string("T2a: ") + name + " is 16-bit");
    }
    check(got1 == want, "T2b: cam1 holds the same three stems");
    std::error_code ec;
    check(!fs::exists(ws / "kept_tmp", ec) && !fs::exists(ws / "frames_tmp", ec) &&
              !fs::exists(ws / "frames16_tmp", ec) &&
              !fs::exists(ws / "track_cam0.mp4", ec) && !fs::exists(ws / "track_cam1.mp4", ec),
          "T2c: no scratch left in the workspace");
    check(!out.captures.empty() && out.captures[0].lockstep,
          "T2c: the capture is recorded lockstep");
}

// More keepers than ffmpeg's expression parser takes as one flat sum (100).
void test_many_keepers(const fs::path& clip) {
    const fs::path ws = scratch("t2d");
    gui::PrepResult out;
    std::string err;
    check(run_prep(job_for(clip, ws, 16, 15.0f, 2), out, err),
          "T2d: 125 keepers prep runs: " + err);
    const std::vector<std::string> got = names_in(ws / "images" / "cam0");
    bool even = got.size() == 125;
    for (size_t i = 0; even && i < got.size(); i++) {
        char name[16];
        std::snprintf(name, sizeof name, "%05d.png", (int)(2 * i));
        even = got[i] == name;
    }
    check(even, "T2d: cam0 holds the 125 even frames (got " + std::to_string(got.size()) + ")");
    check(names_in(ws / "images" / "cam1") == got, "T2d: cam1 holds the same stems");
}

// ---------------------------------------------------------------------------
// T5: the passthrough spelling each ffmpeg accepts
// ---------------------------------------------------------------------------

void test_passthrough_args() {
    auto args = [](const char* line) {
        std::string s;
        for (const std::string& a : gui::passthrough_args_for(line))
            s += (s.empty() ? "" : " ") + a;
        return s;
    };
    const std::string vs = "-vsync passthrough", fm = "-fps_mode passthrough";
    check(args("ffmpeg version 4.4.2-0ubuntu0.22.04.1 Copyright (c) 2000-2021") == vs,
          "T5: 4.4.2 -> -vsync");
    check(args("ffmpeg version 5.0.1 Copyright") == vs, "T5: 5.0.1 -> -vsync");
    check(args("ffmpeg version 5.1 Copyright") == fm, "T5: 5.1 -> -fps_mode");
    check(args("ffmpeg version 9.0.1 Copyright (c) 2000-2026") == fm, "T5: 9.0.1 -> -fps_mode");
    check(args("ffmpeg version n7.0 Copyright") == fm, "T5: n7.0 -> -fps_mode");
    check(args("ffmpeg version n4.3.1 Copyright") == vs, "T5: n4.3.1 -> -vsync");
    check(args("ffmpeg version 10.2 Copyright") == fm, "T5: 10.2 -> -fps_mode");
}

}  // namespace

int main() {
    test_passthrough_args();
    if (!gui::command_exists("ffmpeg")) {
        std::printf("SKIP: ffmpeg not found\n");
        return g_failures ? 1 : 0;
    }
    g_root = fs::temp_directory_path() / "spirula_frame_bits_test";
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    const fs::path clip = g_root / "ramp.osv", longer = g_root / "long.osv";
    const bool have = make_fixture(clip, 6) && make_fixture(longer, 250);
    check(have, "fixture: two lossless 10-bit tracks");
    if (have) {
        test_every_frame_16(clip);
        test_auto_and_forced_8(clip);
        test_selected_16(clip);
        test_many_keepers(longer);
    }
    if (g_failures == 0) fs::remove_all(g_root, ec);
    std::printf(g_failures ? "frame_bits_test: %d FAILED\n" : "frame_bits_test: OK\n",
                g_failures);
    return g_failures ? 1 : 0;
}
