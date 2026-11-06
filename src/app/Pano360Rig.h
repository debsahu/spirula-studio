#pragma once

// What a 360 plan's views tell a reconstruction about one another: they share
// a frame, and each was cut at a known rotation. Spirula Studio's rig row and
// `sam extract --360 faces` both hand this over, so it lives once.

#include "app/Pano360.h"
#include "sfm/core/Rig.h"

#include <vector>

namespace app {

// `d`'s members from the views that have a folder, each starting at its cut
// rotation (a GoPro calibrates 0.1-0.5 deg off them). One lens's EAC views, or
// all of a panorama's, share a centre. False when the counts differ.
inline bool pano360_rig_members(const Pano360Layout& l,
                                const std::vector<Pano360View>& plan,
                                sfm::RigDef& d) {
    std::vector<const Pano360View*> views;
    for (const Pano360View& v : plan)
        if (!v.dir.empty()) views.push_back(&v);
    if (views.empty() || views.size() != d.members.size()) return false;
    const size_t per_lens = l.sphere() ? views.size() : views.size() / 2;
    for (size_t m = 0; m < views.size(); m++) {
        sfm::RigMemberDef& md = d.members[m];
        md.has_ext = true;
        // Pano360View::rot maps a view ray into the camera; cam_from_rig is
        // the other way.
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) md.ext.R[3 * r + c] = views[m]->rot[3 * c + r];
        md.ext.t = {0, 0, 0};
        md.ext_fixed = false;
        md.dof = m < per_lens ? sfm::kRigDofRotation : sfm::kRigDofAll;
    }
    return true;
}

}  // namespace app
