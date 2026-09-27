#pragma once

// Splitting a reconstruction into parts that train separately and merge back:
// a covisibility graph over the cameras (from the model's tracks, from the seed
// cloud projected into the frames, or from camera proximity when there is
// nothing else), the normalized cut of it into parts, the region of space each
// part owns (a LabelField over the points its cameras see), and the ring of
// outside cameras each part borrows so a seam is trained from both sides.
// docs/notes/scene-partition.md has the design and the file format.

#include "core/GraphCut.h"
#include "data/DatasetParser.h"
#include "data/LabelField.h"
#include "data/SparseEdit.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spirula {

enum class CovisibilitySource { Auto = 0, Tracks, Projection, Proximity };
const char* covisibility_source_name(CovisibilitySource s);
bool covisibility_source_from_name(const std::string& s, CovisibilitySource& out);

struct PartitionOptions {
    // Exactly `parts` parts when > 0; otherwise as many as it takes to keep
    // every part at or under `max_images` cameras.
    int parts = 0;
    int max_images = 800;
    // A camera outside a part joins its ring when at least this fraction of
    // the points it sees, and at least `ring_min_points` of them, belong to
    // the part.
    float ring_fraction = 0.05f;
    int ring_min_points = 20;
    // The ownership field keeps at most this many seed points (strided);
    // cameras are always seeds.
    int max_seeds = 1000000;
    CovisibilitySource source = CovisibilitySource::Auto;
    // Projection covisibility subsamples the cloud to this many points.
    int max_projected_points = 200000;
    // Proximity covisibility: neighbours per camera.
    int proximity_neighbours = 8;
};

// Who sees what: point i is seen by frames [beg[i], beg[i+1]) of `frame`.
struct Covisibility {
    std::vector<int64_t> beg;
    std::vector<int32_t> frame;
    graph::WeightedGraph cameras;
    CovisibilitySource source = CovisibilitySource::Proximity;
    int64_t num_points() const { return beg.empty() ? 0 : (int64_t)beg.size() - 1; }
    bool has_tracks() const { return !frame.empty(); }
};

using PartitionLog = std::function<void(const std::string&)>;

// Builds the covisibility the options ask for. `tracks`, when it has any, is
// the model's own (read_sparse_stats); Auto takes tracks, then projection of
// the seed cloud, then proximity. Never throws for a dataset with cameras.
Covisibility build_covisibility(const ParsedDataset& ds, const SparseStats* tracks,
                                const PartitionOptions& opt, const PartitionLog& log = {});

struct ScenePartition {
    int num_parts = 0;
    CovisibilitySource source = CovisibilitySource::Proximity;
    PartitionOptions options;
    // Per frame of the dataset it was made from: the leaf of the image name,
    // which is how a trainer that parsed the folder itself finds them again,
    // and the part whose core the frame is in.
    std::vector<std::string> frame_names;
    std::vector<int32_t> frame_label;
    // Per part: frame indices of its ring, sorted.
    std::vector<std::vector<int32_t>> ring;
    // Per seed point (file order): its owner. Per part: the points it seeds
    // from -- everything its core and ring cameras see, plus what it owns.
    std::vector<int32_t> point_label;
    std::vector<std::vector<int64_t>> part_points;
    // Who owns each point of space (data/LabelField.h), and the cameras'
    // positions [N,3], which orient a splat's normal at the merge.
    std::shared_ptr<LabelField> field;
    std::vector<float> frame_centers;

    bool empty() const { return num_parts == 0; }
    // Core plus ring, sorted.
    std::vector<int32_t> frames_of(int part) const;
    int64_t core_count(int part) const;
    // Connected pieces of the covisibility graph each part's core makes: 1 is
    // a part in one place, more is one the cut could not keep together.
    std::vector<int32_t> core_pieces;
    // Sum of cut edge weight over total edge weight: how much covisibility the
    // partition severed, 0..1.
    double cut_fraction = 0.0;
};

// The partition of `ds` by `cov`. Throws when the dataset has no cameras.
ScenePartition partition_scene(const ParsedDataset& ds, const Covisibility& cov,
                               const PartitionOptions& opt, const PartitionLog& log = {});

// partition.json beside a `.bin` of the same stem holding the volume and the
// point tables. `dataset` is recorded as given.
void write_partition(const ScenePartition& p, const std::string& json_path,
                     const std::string& dataset);
ScenePartition read_partition(const std::string& json_path, std::string* dataset = nullptr);

struct PartitionApplied {
    int64_t frames_before = 0, frames_after = 0, core = 0, ring = 0;
    int64_t points_before = 0, points_after = 0;
    // Frames of the part the dataset did not have (renamed, or filtered by a
    // stricter parse).
    int64_t missing = 0;
};

// Keeps only part `part`'s frames and seed points in `ds`, matched by image
// leaf; train/val indices follow. False when `part` is out of range.
bool apply_partition(ParsedDataset& ds, const ScenePartition& p, int part,
                     PartitionApplied& out);

// A distinct colour per part for displays, 0..1 RGB, stable across runs.
void part_color(int part, float rgb[3]);

}  // namespace spirula
