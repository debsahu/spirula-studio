#pragma once

// The mask editor's SAM half: the checkpoint, the editor's own prompt state and
// a dedicated job thread. Fully pimpl'd and sam::-free, so MaskSession.h -- which
// mask_doc_test compiles -- can hold one without the inference layer existing.
// The whole model call is #ifdef SS_BUILD_SAM inside MaskSam.cpp.

#include "app/gui/mask/MaskAdd.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gui {

struct MaskSettings;

namespace mask {

// Runs `body`; a throw of any kind becomes one `on_throw(reason)` call rather
// than std::terminate on the job thread.
void run_guarded(const std::function<void()>& body,
                 const std::function<void(const std::string&)>& on_throw);

struct SamPoint {
    float x = 0.0f, y = 0.0f;   // frame pixels
    bool positive = true;       // false: "not this", as SegmentPanel's right click
};

class MaskSam {
public:
    MaskSam();
    ~MaskSam();
    MaskSam(const MaskSam&) = delete;
    MaskSam& operator=(const MaskSam&) = delete;

    // False in a build with no inference layer; every start_* then refuses.
    static bool available();
    // The inference layer's whole device pool, MiB, with or without a session;
    // -1 when there is no inference layer.
    static double pool_mib();

    // `text_prompts` is the catalog's word for the checkpoint, trusted until a
    // load answers for itself.
    void set_model(const std::string& path, bool text_prompts);
    const std::string& model_path() const;
    bool has_model() const;
    bool text_supported() const;
    bool busy() const;
    void cancel();
    // Joins any job, hands the weights back and forgets status, error and any
    // untaken result -- true if there was one. Keeps the model path and clicks.
    bool release();
    // The session's own TOTAL, MiB, as of the last job; -1 with no session.
    double vram_mib() const;

    // The editor's own prompt state -- never the dataset screen's.
    MaskSettings& prompt();
    const MaskSettings& prompt() const;
    // A click joins prompt().current_object on `frame` (a MaskSession frame
    // index) and `camera`; object_points() is what a prompt then sends.
    void add_click(long long frame, const std::string& camera, float x, float y,
                   bool positive = true);
    std::vector<SamPoint> object_points(long long frame, const std::string& camera) const;

    // The job co-owns `rgb`, so the caller may replace or drop its own copy at
    // once. `points` are frame pixels; `phrases` is semicolon separated.
    bool start_points(const std::string& frame_key,
                      std::shared_ptr<const std::vector<uint8_t>> rgb, int fw, int fh,
                      std::vector<SamPoint> points, bool keep);
    bool start_text(const std::string& frame_key,
                    std::shared_ptr<const std::vector<uint8_t>> rgb, int fw, int fh,
                    const std::string& phrases);
    // One finished job, or false. `frame_key` is the key it was started on.
    bool take_result(std::string& frame_key, std::vector<AddRegion>& out, bool& keep,
                     float& score, double& ms);

    std::string status() const;
    std::string error() const;
    // A prompt refused before it reached the job: `reason` becomes error().
    void refuse(const std::string& reason);
    // Clears error() only if it still reads `reason`, and says whether it did.
    bool clear_error_if(const std::string& reason);
    // The job's hand-off of a finished result; public so a test can stand in
    // for the job in a build without SAM.
    void post_result(std::string frame_key, std::vector<AddRegion> regions, bool keep,
                     float score, double ms);

private:
    struct State;
    struct Job;
    bool launch(Job job);
    void release_device();   // the SAM-only half of release()
    static void publish(State& s, std::string frame_key, std::vector<AddRegion> regions,
                        bool keep, float score, double ms);
    static void run(State& s, Job job);
    static void run_stages(State& s, Job job,
                           const std::function<void(const std::string&)>& finish);
    std::unique_ptr<State> _s;
};

}  // namespace mask
}  // namespace gui
