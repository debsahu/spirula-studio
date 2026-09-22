// MaskSam.cpp -- see MaskSam.h.

#include "app/gui/mask/MaskSam.h"

#include "app/gui/MaskSettings.h"

#include <mutex>

namespace gui {
namespace mask {

struct MaskSam::State {
    std::string model;
    bool text_hint = false;
    MaskSettings prompt;             // the EDITOR'S own, never the dataset screen's
    mutable std::mutex mu;
    std::string status, error;       // guarded by mu
};

MaskSam::MaskSam() : _s(std::make_unique<State>()) {}
MaskSam::~MaskSam() { release(); }

void MaskSam::set_model(const std::string& path, bool text_prompts) {
    _s->model = path;
    _s->text_hint = text_prompts;
}

bool MaskSam::has_model() const { return !_s->model.empty(); }
MaskSettings& MaskSam::prompt() { return _s->prompt; }
const MaskSettings& MaskSam::prompt() const { return _s->prompt; }

void MaskSam::add_click(long long frame, const std::string& camera, float x, float y) {
    MaskClick c;
    c.x = x;
    c.y = y;
    c.object = _s->prompt.current_object;
    c.frame = frame;
    c.camera = camera;
    _s->prompt.clicks.push_back(std::move(c));
}

// `source` stays empty: an editor click is never handed to a dataset run, so it
// needs no input path, and empty keeps SegmentPanel's filter from ever matching.
std::vector<SamPoint> MaskSam::object_points(long long frame, const std::string& camera) const {
    std::vector<SamPoint> out;
    for (const MaskClick& c : _s->prompt.clicks)
        if (c.positive && c.source.empty() && c.object == _s->prompt.current_object &&
            c.frame == frame && c.camera == camera)
            out.push_back(SamPoint{c.x, c.y});
    return out;
}

std::string MaskSam::status() const {
    std::lock_guard<std::mutex> lk(_s->mu);
    return _s->status;
}

std::string MaskSam::error() const {
    std::lock_guard<std::mutex> lk(_s->mu);
    return _s->error;
}

#ifdef SS_BUILD_SAM

bool MaskSam::available() { return true; }
double MaskSam::pool_mib() { return -1.0; }
bool MaskSam::text_supported() const { return false; }
bool MaskSam::busy() const { return false; }
void MaskSam::cancel() {}
void MaskSam::release() {}
double MaskSam::vram_mib() const { return -1.0; }

bool MaskSam::start_points(const std::string&, std::shared_ptr<const std::vector<uint8_t>>, int,
                           int, std::vector<SamPoint>, bool) {
    return false;
}
bool MaskSam::start_text(const std::string&, std::shared_ptr<const std::vector<uint8_t>>, int,
                         int, const std::string&) {
    return false;
}
bool MaskSam::take_result(std::string&, std::vector<AddRegion>&, bool&, float&, double&) {
    return false;
}

#endif  // SS_BUILD_SAM

#ifndef SS_BUILD_SAM

bool MaskSam::available() { return false; }
double MaskSam::pool_mib() { return -1.0; }
bool MaskSam::text_supported() const { return false; }
bool MaskSam::busy() const { return false; }
void MaskSam::cancel() {}
void MaskSam::release() {}
double MaskSam::vram_mib() const { return -1.0; }

bool MaskSam::start_points(const std::string&, std::shared_ptr<const std::vector<uint8_t>>, int,
                           int, std::vector<SamPoint>, bool) {
    return false;
}
bool MaskSam::start_text(const std::string&, std::shared_ptr<const std::vector<uint8_t>>, int,
                         int, const std::string&) {
    return false;
}
bool MaskSam::take_result(std::string&, std::vector<AddRegion>&, bool&, float&, double&) {
    return false;
}

#endif  // !SS_BUILD_SAM

}  // namespace mask
}  // namespace gui
