// dataset_color -- what `--image-color-log auto` makes of a dataset's record of
// its clips' picture profiles (data/DatasetColor.h), and that load_dataset is
// what reads it. An explicit value, `none` included, is never overridden.

#include "app/TrainerCore.h"
#include "external/stb_image_write.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace spirula;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) g_failures++;
}

DatasetColor record(std::initializer_list<ClipColor> modes) {
    DatasetColor d;
    int i = 0;
    for (ClipColor m : modes)
        d.clips.push_back({m, m == ClipColor::DlogM ? 19 : m == ClipColor::Normal ? 0 : -1,
                           "clip" + std::to_string(i++) + ".OSV"});
    return d;
}

colorspace::InputCurve curve_after(TrainConfig c, const DatasetColor& d, std::string* line = nullptr) {
    const std::string l = adopt_dataset_color(c, d);
    if (line) *line = l;
    return resolve_color(c).image_curve;
}

bool refused(TrainConfig c, const DatasetColor& d) {
    try { adopt_dataset_color(c, d); } catch (const std::exception&) { return true; }
    return false;
}

void write_colmap(const fs::path& root) {
    constexpr int kW = 16, kH = 12;
    fs::create_directories(root / "images");
    fs::create_directories(root / "sparse" / "0");
    std::vector<uint8_t> px((size_t)kW * kH * 3, 102);
    std::ofstream im(root / "sparse" / "0" / "images.txt");
    for (int i = 0; i < 3; i++) {
        const std::string name = "v" + std::to_string(i) + ".png";
        stbi_write_png((root / "images" / name).string().c_str(), kW, kH, 3, px.data(), kW * 3);
        im << i + 1 << " 1 0 0 0 " << 0.2 * i << " 0 0 1 " << name << "\n\n";
    }
    std::ofstream(root / "sparse" / "0" / "cameras.txt") << "1 PINHOLE 16 12 15 15 8 6\n";
    std::ofstream pts(root / "sparse" / "0" / "points3D.txt");
    for (int k = 0; k < 16; k++)
        pts << k + 1 << " " << 0.1 * (k % 4) << " " << 0.1 * (k / 4) << " 4 102 102 102 0.5\n";
}

}  // namespace

int main() {
    using C = ClipColor;
    using IC = colorspace::InputCurve;
    const TrainConfig unset;
    check(unset.image_color_log == "auto", "config: unset is `auto`, distinct from an explicit none");

    std::string line;
    check(curve_after(unset, record({C::DlogM, C::DlogM}), &line) == IC::DlogMOsmo360,
          "default: a D-Log M dataset decodes as dlogm-osmo360");
    check(line.find("dlogm-osmo360") != std::string::npos, "default: the start line names the curve");
    {
        TrainConfig c = unset;
        adopt_dataset_color(c, record({C::DlogM}));
        check(resolve_color(c).point_curve == IC::DlogMOsmo360,
              "default: the SfM seed colours follow the images");
    }

    TrainConfig cli_none = unset, gui_none = unset;
    cli_none.image_color_log = "";      // `--image-color-log none` as the CLI parses it
    gui_none.image_color_log = "none";  // as the GUI's combo writes it
    check(curve_after(cli_none, record({C::DlogM}), &line) == IC::None &&
              curve_after(gui_none, record({C::DlogM})) == IC::None,
          "explicit none: wins over a D-Log M dataset");
    check(line.find("none") != std::string::npos, "explicit none: the start line says it was set");

    TrainConfig explicit_log = unset;
    explicit_log.image_color_log = "dlogm-osmo360";
    check(curve_after(explicit_log, record({C::Normal})) == IC::DlogMOsmo360,
          "explicit dlogm-osmo360: wins over a Normal dataset");

    check(refused(unset, record({C::DlogM, C::Normal})), "mixed: D-Log M beside Normal is refused");
    check(refused(unset, record({C::DlogM, C::Unknown})), "mixed: D-Log M beside an unknown clip is refused");
    check(refused(unset, record({C::DlogM, C::NotRecorded})),
          "mixed: D-Log M beside a clip with no profile metadata is refused");
    check(!refused(cli_none, record({C::DlogM, C::Normal})) &&
              !refused(explicit_log, record({C::DlogM, C::Normal})),
          "mixed: an explicit value is not refused");

    {
        TrainConfig c = unset;
        adopt_dataset_color(c, record({C::Normal, C::Other}));
        TrainConfig plain = unset;
        plain.image_color_log = "";
        const ColorResolution a = resolve_color(c), b = resolve_color(plain);
        check(a.image_curve == IC::None && a.point_curve == IC::None && a.image_linear == b.image_linear &&
                  a.image_gamut == b.image_gamut && c.image_color_log.empty(),
              "normal: a Normal dataset is read exactly as with no flag");
    }

    check(curve_after(unset, record({C::Unknown}), &line) == IC::None,
          "unknown (Avata 360): no automatic default");
    check(line.find("--image-color-log") != std::string::npos && line.find("clip0.OSV") != std::string::npos,
          "unknown (Avata 360): the start line names the clip and asks for the flag");
    check(curve_after(unset, DatasetColor{}, &line) == IC::None && line.empty(),
          "no record: nothing changes and nothing is said");

    const fs::path tmp = fs::temp_directory_path() / ("dataset_color_test_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    write_dataset_color(tmp.string(), record({C::DlogM, C::Unknown}));
    const DatasetColor back = read_dataset_color(tmp.string());
    check(back.clips.size() == 2 && back.clips[0].mode == C::DlogM && back.clips[0].code == 19 &&
              back.clips[1].mode == C::Unknown && back.clips[1].source == "clip1.OSV",
          "record: written and read back");
    write_dataset_color(tmp.string(), record({C::NotRecorded}));
    check(!fs::exists(tmp / kDatasetColorFile), "record: nothing to say removes a stale one");

    const fs::path data = tmp / "data";
    write_colmap(data);
    write_dataset_color(data.string(), record({C::DlogM}));
    TrainerSession s;
    s.cfg.data = data.string();
    s.log_fn = [](const std::string&) {};
    s.load_dataset();
    check(s.cfg.image_color_log == "dlogm-osmo360", "session: load_dataset adopts the dataset's record");

    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("%s\n", g_failures ? "FAILED" : "all ok");
    return g_failures ? 1 : 0;
}
