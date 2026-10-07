#pragma once
// Getting a checkpoint onto disk: the shared cache directory, the download,
// and the SHA-256 that has to match before a parser sees the bytes.
//
// The URL tables stay with the models (aliked/model/Fetch.h,
// metric3d/model/Fetch.h) because which artifact to fetch is a model decision.
// Everything below that -- where it lands, how it is verified, what happens to
// a half-finished download -- is not, and there is one copy of it here.
//
// Nothing is bundled: the upstream URL is tried first, so the bytes we run are
// the bytes the reference implementation runs. The fallback mirror
// (core/ModelMirror.h) holds identical files, and the SHA-256 holds it to that.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace nn {

// One artifact. `sha256` is lowercase hex, and is checked after download and
// on every subsequent load of the cached file -- a truncated or tampered
// artifact must not reach the parser.
struct FetchFile {
    const char* file = nullptr;    // basename in the cache directory
    const char* url = nullptr;
    const char* sha256 = nullptr;
    uint64_t    bytes = 0;         // approximate, for the "downloading N MB" line
    // A second host that already carries the same bytes; null for the
    // project's own mirror (core/ModelMirror.h), which re-hosts under `file`.
    const char* mirror = nullptr;
    // Licences the user must have accepted before this file is fetched or
    // loaded (core/LicenseConsent.h): one family or a comma list; null for none.
    const char* license_family = nullptr;
    // True for a file that must come from `url` alone: re-hosting it would be
    // distributing it.
    bool no_mirror = false;
};

// Where `f` is fetched from when `url` fails.
std::string mirror_url(const FetchFile& f);

// <cache>/spirula-studio/models. Mirrors src/app/AppPaths.cpp's
// cache_dir(); duplicated rather than shared because src/nn/ sits below
// src/app/ in the layering and may not include it.
std::string model_cache_dir();
std::string cached_path(const FetchFile& f);

// A verified local copy, fetched with the system `curl` from `url`, then the
// mirror, if missing; `tag` prefixes its progress lines. Throws nn::Error naming
// both URLs -- and, with SS_NO_AUTO_FETCH set, instead of downloading at all.
std::string ensure_file(const FetchFile& f, const char* tag);

// ---- Licence consent (the CLI half of the GUI's accept dialog) ----
//
// ensure_file() on a file with a `license_family` throws, before it touches the
// network or the cache, unless the family is in gui.conf's accepted_license=
// list. A tool accepts on the user's behalf only through `--accept-license`.

struct ConsentIO {
    std::FILE*                       out = stdout;  // the terms are printed here
    bool                             tty = false;   // may ask on stdin
    std::function<std::string()>     read_line;     // one answer; used when `tty`
};

// A `--accept-license` value: a comma list of `family` (asks "yes" on a TTY,
// refuses otherwise) or `family=yes` (no question; for scripts). Prints each
// family's full terms first, then records the acceptance in gui.conf. Throws
// nn::Error on an unknown family, a declined or unanswerable question.
void accept_licenses(const std::string& spec, const ConsentIO& io);

// Removes every `--accept-license <spec>` / `--accept-license=<spec>` from
// argv, accepts what they name on stdin/stdout, and returns how many it found.
int consume_accept_license_args(int& argc, char** argv);

// Throws nn::Error naming the family and the flag that accepts it, unless
// `family` is accepted. A null or empty family needs nothing.
void require_license(const char* family);

// Lowercase hex SHA-256 of a file's contents. Empty when it cannot be read.
std::string sha256_file(const std::string& path);

}  // namespace nn
