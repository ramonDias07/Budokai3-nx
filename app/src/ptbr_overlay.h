#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// Build 0.1.52: optional PT-BR translation, served as a virtual overlay of the game's AFS archives.
//
// The pack (traducao/ptbr.b3tr, format B3TR v1, see tools/ptbr/b3tr.py) holds replacement payloads for
// some AFS entries. When it is valid, every archive it names is presented to the guest with a FIXED
// virtual layout built once at boot: the same header region, the TOC rewritten so that each replaced
// entry gets a slot of max(original, translated) bytes (rounded to 0x800) and everything after it
// shifted. The toggle only changes which bytes fill a replaced slot (the original LZX stream or the
// translated payload, then zero padding); the TOC and the file size never change, so flipping it while
// the game runs is safe even though the game keeps the TOC it read at boot. Entries already in memory
// stay until the game loads them again.
namespace b3::ptbr {

enum class Status { kNotInstalled, kUnavailable, kReady };

// Loads and validates the pack against the files in us_dir and, when valid, installs the overlay in the VFS.
// Call before the runtime mounts the game root. Only the first call does anything.
// max_growth_mb > 0 leaves out entries that grow by more than that over the original entry (see Pack::set_max_growth).
// activate = the translation is on at boot. Only then is the virtual layout installed; otherwise the pack is just
// validated (so the menu can tell whether it is installed) and the game files are served exactly as without it.
Status Initialize(const std::filesystem::path& pack_path, const std::filesystem::path& us_dir, int max_growth_mb,
                  bool activate);
Status GetStatus();
// True when the virtual layout is installed (translation was on at boot): only then does SetEnabled switch live.
bool Active();

// The toggle. Stays off unless the layout is active.
void SetEnabled(bool enabled);
bool Enabled();

// ---- Core, exposed for the host-side test (no ReXGlue types below) ----------------------------------------------
struct Segment {
  enum Kind : uint8_t { kHeader, kOriginal, kReplaced };
  uint64_t virtual_start = 0;
  uint64_t length = 0;           // virtual bytes covered
  uint64_t original_start = 0;   // kOriginal/kReplaced: first byte in the real file
  uint64_t original_length = 0;  // kReplaced: original bytes shown while the toggle is off (then zeros)
  int32_t pack_entry = -1;       // kReplaced: index in the pack entry table
  uint32_t entry_index = 0;      // kReplaced: AFS entry
  uint32_t translated_size = 0;  // kReplaced
  Kind kind = kOriginal;
};

struct Layout {
  std::vector<uint8_t> header;    // virtual bytes [0, header.size()): the real header with the TOC rewritten
  std::vector<Segment> segments;  // sorted and contiguous; the first one is the header
  uint64_t real_size = 0;
  uint64_t virtual_size = 0;
  uint32_t entry_count = 0;
  uint32_t replaced = 0;
};

struct Replacement {
  uint32_t entry_index;
  uint32_t size;  // translated payload size
  int32_t pack_entry;
};

// Builds the virtual layout of an AFS archive. head = the first bytes of the real file. If it is too short,
// returns false with *need_head set to the number of bytes required (call again with more).
bool BuildLayout(std::span<const uint8_t> head, uint64_t real_size, std::span<const Replacement> replacements,
                 Layout* out, std::string* error, size_t* need_head);

uint32_t Crc32(std::span<const uint8_t> data, uint32_t crc = 0);

using Payload = std::shared_ptr<const std::vector<uint8_t>>;

// Serves virtual reads of one open file. One instance per open handle (not thread-safe on its own, the kernel
// serializes reads per handle).
class VirtualReader {
 public:
  // original(offset, dst, n) returns the bytes read from the real file (short only at its end), or SIZE_MAX if the
  // read failed (then Read stops early and failed() is true).
  // translated(pack_entry) returns the decompressed payload or nullptr (then the original bytes are served).
  // enabled() is the global toggle.
  VirtualReader(const Layout* layout, std::function<size_t(uint64_t, uint8_t*, size_t)> original,
                std::function<Payload(int32_t)> translated, std::function<bool()> enabled)
      : layout_(layout), original_(std::move(original)), translated_(std::move(translated)),
        enabled_(std::move(enabled)) {}

  // Returns the bytes copied (0 at or past the virtual end of file).
  size_t Read(uint64_t offset, uint8_t* dst, size_t length);

  // True if the last Read stopped early because the real file could not be read; Read returned the bytes done.
  bool failed() const { return failed_; }

 private:
  // A replaced entry read in several calls (the game reads big entries 256 sectors at a time) keeps the state it
  // started with, so a toggle in the middle cannot mix original and translated bytes in one load.
  bool Translate(const Segment& segment, uint64_t offset, uint64_t length);
  // Returns false if the real file failed to read (failed_ is set).
  bool ReadOriginal(uint64_t offset, uint8_t* dst, size_t length);

  const Layout* layout_;
  std::function<size_t(uint64_t, uint8_t*, size_t)> original_;
  std::function<Payload(int32_t)> translated_;
  std::function<bool()> enabled_;
  const Segment* latched_segment_ = nullptr;
  uint64_t latched_next_ = 0;
  bool latched_on_ = false;
  Payload latched_payload_;  // held for the whole load of one entry: one cache lookup per entry, not per chunk
  bool failed_ = false;
};

// ---- Pack ------------------------------------------------------------------------------------------------------
struct PackFile {
  std::string name;            // lower case, under game_root/us/
  std::filesystem::path path;  // the real file
  uint32_t original_size = 0;
  uint32_t afs_entry_count = 0;
  uint32_t original_crc32 = 0;
  Layout layout;
};

struct PackEntry {
  uint16_t file_id = 0;
  uint16_t codec = 0;  // 0 raw, 1 zstd
  uint32_t entry_index = 0;
  uint64_t data_offset = 0;
  uint32_t stored_size = 0;
  uint32_t size = 0;
  uint32_t crc32 = 0;
};

class Pack {
 public:
  ~Pack();
  // Reads the tables and validates them against the files in us_dir; builds every layout. False with a reason
  // (English, for the log) on any mismatch.
  bool Open(const std::filesystem::path& pack_path, const std::filesystem::path& us_dir, std::string* error);

  const std::vector<PackFile>& files() const { return files_; }
  const std::vector<PackEntry>& entries() const { return entries_; }

  // Decompressed payload of an entry, from the LRU cache or the pack. nullptr if it cannot be produced (logged once).
  Payload Get(int32_t pack_entry);

  // Bound of the decompressed cache. An entry bigger than this is still served (it is kept alone).
  void set_cache_limit(size_t bytes) { cache_limit_ = bytes; }

  // Before Open: leave out entries whose translated size exceeds the original (LZX) entry by more than this many
  // bytes (0 = keep all). They stay in English and cost no extra guest memory.
  void set_max_growth(uint64_t bytes) { max_growth_ = bytes; }
  const std::vector<int32_t>& dropped() const { return dropped_; }  // pack entries left out by the cap

  struct Stats {
    uint64_t decompressed = 0;     // payloads produced from the pack
    uint64_t hits = 0;             // served from the cache
    uint64_t evictions = 0;
    uint64_t cache_bytes = 0;
    uint64_t cache_peak = 0;
    uint64_t failures = 0;
  };
  Stats stats() const;

 private:
  struct Cached {
    Payload data;
    uint64_t use = 0;
  };
  std::FILE* file_ = nullptr;
  uint64_t pack_size_ = 0;
  std::vector<PackFile> files_;
  std::vector<PackEntry> entries_;
  std::vector<Cached> cache_;    // indexed by pack entry
  std::vector<uint8_t> failed_;  // indexed by pack entry
  size_t cache_limit_ = size_t(32) << 20;
  uint64_t max_growth_ = 0;
  std::vector<int32_t> dropped_;
  void* dctx_ = nullptr;          // ZSTD_DCtx, reused under io_mutex_
  mutable std::mutex cache_mutex_;
  std::mutex io_mutex_;
  uint64_t use_clock_ = 0;
  Stats stats_;
};

}  // namespace b3::ptbr
