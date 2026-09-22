// MaskSam.cpp -- see MaskSam.h.

#include "app/gui/mask/MaskSam.h"

#include "app/gui/MaskSettings.h"

#include <mutex>

#ifdef SS_BUILD_SAM
#include "i18n/catalog/MaskEdit.h"
#include "nn/vk/Memory.h"
#include "sam/Masking.h"   // split_phrases, downscale_to_fit -- never sam::Masker
#include "sam/Sam.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#endif

namespace gui {
namespace mask {

struct MaskSam::State {
    std::string model;
    bool text_hint = false;
    MaskSettings prompt;             // the EDITOR'S own, never the dataset screen's
    mutable std::mutex mu;
    std::string status, error;       // guarded by mu
#ifdef SS_BUILD_SAM
    // The job thread's alone while `running`; the UI thread's after a join.
    std::unique_ptr<sam::Session> session;
    std::string loaded_model, encoded_key;
    std::thread worker;              // UI thread only
    std::atomic<bool> running{false}, cancel{false};
    bool loaded = false;             // guarded by mu, with everything below
    bool loaded_text = false;
    double vram_mib = -1.0;
    bool ready = false;
    std::string result_key;
    std::vector<AddRegion> result;
    bool result_keep = false;
    float result_score = 0.0f;
    double result_ms = 0.0;
#endif
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

void MaskSam::add_click(long long frame, const std::string& camera, float x, float y,
                        bool positive) {
    MaskClick c;
    c.x = x;
    c.y = y;
    c.positive = positive;
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
        if (c.source.empty() && c.object == _s->prompt.current_object && c.frame == frame &&
            c.camera == camera)
            out.push_back(SamPoint{c.x, c.y, c.positive});
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

namespace msg = spirula::i18n::msg::maskedit;

struct MaskSam::Job {
    std::string model, frame_key, phrases;
    std::shared_ptr<const std::vector<uint8_t>> rgb;
    int fw = 0, fh = 0;
    std::vector<SamPoint> points;
    bool keep = false, text = false;
    int max_size = 0;
    float score_threshold = 0.5f, nms_threshold = 0.1f;
};

namespace {

// The MiB on the TOTAL line of Session::vramReport(); -1 when there is none.
double total_mib(const std::string& report) {
    const size_t at = report.find("TOTAL");
    return at == std::string::npos ? -1.0 : std::strtod(report.c_str() + at + 5, nullptr);
}

}  // namespace

bool MaskSam::available() { return true; }

double MaskSam::pool_mib() {
    return (double)nn::vk::VramPool::get().totalCapacity() / 1048576.0;
}

bool MaskSam::text_supported() const {
    std::lock_guard<std::mutex> lk(_s->mu);
    return _s->loaded ? _s->loaded_text : _s->text_hint;
}

bool MaskSam::busy() const { return _s->running.load(); }

void MaskSam::cancel() { _s->cancel = true; }

void MaskSam::release() {
    _s->cancel = true;
    if (_s->worker.joinable()) _s->worker.join();
    if (_s->session) _s->session->unload();
    _s->session.reset();
    _s->loaded_model.clear();
    _s->encoded_key.clear();
    std::lock_guard<std::mutex> lk(_s->mu);
    _s->loaded = false;
    _s->loaded_text = false;
    _s->vram_mib = -1.0;
    _s->ready = false;
    _s->result.clear();
    _s->status.clear();
}

double MaskSam::vram_mib() const {
    std::lock_guard<std::mutex> lk(_s->mu);
    return _s->vram_mib;
}

bool MaskSam::start_points(const std::string& frame_key,
                           std::shared_ptr<const std::vector<uint8_t>> rgb, int fw, int fh,
                           std::vector<SamPoint> points, bool keep) {
    // SAM needs a "this" to exclude a "not this" from.
    if (std::none_of(points.begin(), points.end(), [](const SamPoint& p) { return p.positive; }))
        return false;
    Job j;
    j.frame_key = frame_key;
    j.rgb = std::move(rgb);
    j.fw = fw;
    j.fh = fh;
    j.points = std::move(points);
    j.keep = keep;
    return launch(std::move(j));
}

// A phrase names what to DROP. The editor's own settings cap the encode and
// set the thresholds, read here on the UI thread so the job never touches them.
bool MaskSam::start_text(const std::string& frame_key,
                         std::shared_ptr<const std::vector<uint8_t>> rgb, int fw, int fh,
                         const std::string& phrases) {
    if (!text_supported()) return false;
    Job j;
    j.frame_key = frame_key;
    j.rgb = std::move(rgb);
    j.fw = fw;
    j.fh = fh;
    j.phrases = phrases;
    j.text = true;
    j.max_size = _s->prompt.max_image_size;
    j.score_threshold = _s->prompt.threshold;
    j.nms_threshold = _s->prompt.nms;
    return launch(std::move(j));
}

bool MaskSam::take_result(std::string& frame_key, std::vector<AddRegion>& out, bool& keep,
                          float& score, double& ms) {
    std::lock_guard<std::mutex> lk(_s->mu);
    if (!_s->ready) return false;
    _s->ready = false;
    frame_key = std::move(_s->result_key);
    out = std::move(_s->result);
    keep = _s->result_keep;
    score = _s->result_score;
    ms = _s->result_ms;
    return true;
}

// Refuses while a job runs, so at most one frame buffer is ever held here; a
// refused job's reference dies with `j` on return.
bool MaskSam::launch(Job j) {
    if (_s->model.empty() || !j.rgb || j.fw <= 0 || j.fh <= 0 ||
        j.rgb->size() != (size_t)j.fw * (size_t)j.fh * 3u || busy())
        return false;
    if (_s->worker.joinable()) _s->worker.join();
    j.model = _s->model;
    const bool warm = _s->session && _s->loaded_model == j.model;
    {
        std::lock_guard<std::mutex> lk(_s->mu);
        _s->error.clear();
        _s->status = warm ? msg::sam_working.get() : msg::sam_first_load.get();
    }
    _s->cancel = false;
    _s->running = true;
    State* s = _s.get();
    _s->worker = std::thread([s, j = std::move(j)]() mutable { run(*s, std::move(j)); });
    return true;
}

// The job thread. `cancel` is read between every stage; encodeImage is one
// opaque call, so a cancel issued in it waits it out: up to 1.9 s at 15520x7760.
void MaskSam::run(State& s, Job j) {
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&s](const std::string& error) {
        {
            std::lock_guard<std::mutex> lk(s.mu);
            s.status.clear();
            s.error = error;
        }
        s.running = false;
    };
    if (!s.session || s.loaded_model != j.model) {
        if (s.session) s.session->unload();
        s.session = std::make_unique<sam::Session>();
        s.loaded_model.clear();
        s.encoded_key.clear();
        sam::ModelParams p;
        p.model_path = j.model;
        if (!s.session->loadModel(p)) {
            const std::string e = s.session->lastError();
            s.session.reset();
            finish(e);
            return;
        }
        s.loaded_model = j.model;
        const double mib = total_mib(s.session->vramReport());
        std::lock_guard<std::mutex> lk(s.mu);
        s.loaded = true;
        s.loaded_text = s.session->supportsTextPrompts();
        s.vram_mib = mib;
        s.status = msg::sam_working.get();
    }
    if (s.cancel || (j.text && !s.session->supportsTextPrompts())) {
        finish(std::string());
        return;
    }
    // Masks come back at the ENCODED size, so a capped text encode and a full
    // click encode of the same frame are different keys; switching re-encodes.
    const std::string key = j.frame_key + (j.text ? "#capped" : "#full");
    if (s.encoded_key != key) {
        s.encoded_key.clear();
        nn::Image img;
        img.width = j.fw;
        img.height = j.fh;
        img.channels = 3;
        img.data.assign(j.rgb->begin(), j.rgb->end());
        j.rgb.reset();
        if (j.text) img = sam::downscale_to_fit(img, j.max_size);
        if (s.cancel) {
            finish(std::string());
            return;
        }
        if (!s.session->encodeImage(img)) {
            finish(s.session->lastError());
            return;
        }
        s.encoded_key = key;
    }
    j.rgb.reset();
    sam::Result r;
    if (j.text) {
        for (const std::string& phrase : sam::split_phrases(j.phrases)) {
            if (s.cancel) break;
            sam::ConceptPrompt cp;
            cp.text = phrase;
            cp.score_threshold = j.score_threshold;
            cp.nms_threshold = j.nms_threshold;
            sam::Result one = s.session->segmentConcept(cp);
            for (sam::Detection& d : one.detections) r.detections.push_back(std::move(d));
        }
    } else if (!s.cancel) {
        sam::VisualPrompt vp;
        for (const SamPoint& p : j.points)
            (p.positive ? vp.pos_points : vp.neg_points).push_back(sam::Point{p.x, p.y});
        r = s.session->segmentVisual(vp);
    }
    if (s.cancel) {
        finish(std::string());
        return;
    }
    if (r.detections.empty() && !s.session->lastError().empty()) {
        finish(s.session->lastError());
        return;
    }
    std::vector<AddRegion> out;
    float best = 0.0f;
    for (sam::Detection& d : r.detections) {
        AddRegion g;
        g.w = d.mask.width;
        g.h = d.mask.height;
        g.mask = std::move(d.mask.data);
        g.score = d.score;
        best = std::max(best, d.score);
        out.push_back(std::move(g));
    }
    const double mib = total_mib(s.session->vramReport());
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.vram_mib = mib;
        s.result_key = j.frame_key;
        s.result = std::move(out);
        s.result_keep = j.keep;
        s.result_score = best;
        s.result_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        s.ready = true;
        s.status.clear();
    }
    s.running = false;
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
