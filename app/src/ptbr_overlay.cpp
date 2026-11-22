#include "ptbr_overlay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <system_error>

#include <zstd.h>

#if defined(__aarch64__)
#include <arm_acle.h>
#endif

#ifdef B3_PTBR_HOST_TEST
#define PTBR_ERROR(...) std::printf("[error] %s\n", Fmt(__VA_ARGS__).c_str())
#define PTBR_WARN(...) std::printf("[warn] %s\n", Fmt(__VA_ARGS__).c_str())
#else
#define PTBR_ERROR(...) REXLOG_ERROR("{}", Fmt(__VA_ARGS__))
#define PTBR_WARN(...) REXLOG_WARN("{}", Fmt(__VA_ARGS__))
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/file_overlay.h>
#include <rex/logging.h>
#include <rex/system/xtypes.h>
#endif

/*
 * Build 0.1.52: PT-BR translation overlay. See ptbr_overlay.h for the idea; the evidence behind the design:
 *
 * - How the game loads an AFS entry (sub_82083ED8 / sub_82084210 in app/generated): it opens it by partition and
 *   file id (sub_821DD590), asks its size in SECTORS (sub_821DD190) and allocates and reads sectors << 11 bytes, 256
 *   sectors per read. So every entry already arrives with up to 2047 bytes of trailing padding, and the size it
 *   reads comes from the TOC the game loaded (CRI ADXF partition table), not from the data.
 * - After the read (sub_82084210 ~loc_82084514) the buffer goes through sub_820A4700: if it starts with the XMem
 *   header (0x0FF512EE, version 1.3) it is decompressed with the sizes in that header (sub_820A4748 reads +28, the
 *   uncompressed size; the worker sub_820A47D8 loops over blocks until that many bytes are out). Bytes after the
 *   LZX stream are never looked at, so an original LZX entry inside a bigger slot (toggle off) decodes the same.
 *   Without the header the buffer is used as it is (raw entry): the translated payloads are raw.
 * - The game reads the TOC once per partition, so the TOC must not change while it runs: hence the fixed layout.
 *
 * Every 360 archive checked (data_usi, data_cmn, lang_usa) has its entries sorted, 0x800-aligned and packed with no
 * gaps, then the name table (48-byte records) at the end, pointed to by the pair after the TOC. The name table is
 * moved with the tail and its pointer patched; its records are copied as they are.
 */

namespace b3::ptbr {

namespace {
constexpr uint64_t kSector = 0x800;
constexpr size_t kProbe = 64 * 1024;

uint64_t RoundUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p)) | uint64_t(Le32(p + 4)) << 32; }
void Put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

std::string Fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return buffer;
}

bool ReadAt(std::FILE* f, uint64_t offset, void* dst, size_t n) {
  if (std::fseek(f, long(offset), SEEK_SET) != 0) return false;
  return std::fread(dst, 1, n, f) == n;
}
}  // namespace

namespace {
// Standard CRC-32 (reflected polynomial 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF): the table version.
uint32_t Crc32Table(std::span<const uint8_t> data, uint32_t crc) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

#if defined(__aarch64__)
// Build 0.1.56: the ARMv8 CRC32 instructions (__crc32d/__crc32b) compute the same CRC-32 (polynomial 0xEDB88320,
// reflected); the init/xorout are applied here exactly as in the table version.
__attribute__((target("+crc"))) uint32_t Crc32Hw(std::span<const uint8_t> data, uint32_t crc) {
  crc = ~crc;
  const uint8_t* p = data.data();
  size_t n = data.size();
  while (n >= 8) {
    uint64_t w;
    std::memcpy(&w, p, 8);  // little endian: the same byte order as the byte-at-a-time table
    crc = __crc32d(crc, w);
    p += 8;
    n -= 8;
  }
  while (n--) crc = __crc32b(crc, *p++);
  return ~crc;
}

// Compares both on a small buffer, with an unaligned start and a tail, and on a continued CRC (non-zero seed).
bool Crc32HwMatches() {
  uint8_t buf[67];
  uint32_t x = 0x12345678u;
  for (auto& b : buf) {
    x = x * 1664525u + 1013904223u;
    b = uint8_t(x >> 24);
  }
  for (size_t off : {size_t(0), size_t(1)}) {
    const std::span<const uint8_t> view(buf + off, sizeof(buf) - off);
    if (Crc32Hw(view, 0) != Crc32Table(view, 0) || Crc32Hw(view, 0xDEADBEEFu) != Crc32Table(view, 0xDEADBEEFu)) {
      return false;
    }
  }
  return Crc32Hw({}, 0) == Crc32Table({}, 0);
}
#endif
}  // namespace

uint32_t Crc32(std::span<const uint8_t> data, uint32_t crc) {
#if defined(__aarch64__)
  static const bool hardware = [] {
    const bool ok = Crc32HwMatches();
    if (!ok) PTBR_WARN("[ptbr] hardware CRC32 self-test failed; using the table version");
    return ok;
  }();
  if (hardware) return Crc32Hw(data, crc);
#endif
  return Crc32Table(data, crc);
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------------
bool BuildLayout(std::span<const uint8_t> head, uint64_t real_size, std::span<const Replacement> replacements,
                 Layout* out, std::string* error, size_t* need_head) {
  *need_head = 0;
  if (head.size() < 8 || std::memcmp(head.data(), "AFS\0", 4) != 0) {
    *error = "not an AFS archive";
    return false;
  }
  const uint32_t count = Le32(head.data() + 4);
  const uint64_t toc_end = 8 + uint64_t(count) * 8 + 8;  // TOC plus the name-table pair
  if (count == 0 || toc_end > real_size) {
    *error = Fmt("bad entry count %u", count);
    return false;
  }
  if (head.size() < toc_end) {
    *need_head = size_t(toc_end);
    return false;
  }
  struct Item {
    uint64_t offset, size;
    uint32_t index;
    int32_t replacement;  // index in `replacements` or -1
  };
  std::vector<Item> items;
  std::vector<int32_t> replacement_of(count, -1);
  for (size_t r = 0; r < replacements.size(); ++r) {
    const uint32_t index = replacements[r].entry_index;
    if (index >= count || replacement_of[index] >= 0) {
      *error = Fmt("replacement for entry %u out of range or repeated", index);
      return false;
    }
    replacement_of[index] = int32_t(r);
  }
  uint64_t header_end = UINT64_MAX, data_end = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t offset = Le32(head.data() + 8 + i * 8);
    const uint64_t size = Le32(head.data() + 12 + i * 8);
    if (offset < toc_end) {
      // A placeholder (offset 0) that points at no data: kept as it is.
      if (size != 0 || replacement_of[i] >= 0) {
        *error = Fmt("entry %u points inside the header", i);
        return false;
      }
      continue;
    }
    if (offset + size > real_size) {
      *error = Fmt("entry %u ends past the end of the file", i);
      return false;
    }
    items.push_back({offset, size, i, replacement_of[i]});
    header_end = std::min(header_end, offset);
    data_end = std::max(data_end, offset + size);
  }
  if (items.empty()) {
    *error = "archive without data entries";
    return false;
  }
  if (head.size() < header_end) {
    *need_head = size_t(header_end);
    return false;
  }
  // Zero-size entries first among equal offsets, so the entry that owns the bytes gets the span.
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return a.offset != b.offset ? a.offset < b.offset : a.size != b.size ? a.size < b.size : a.index < b.index;
  });
  const uint64_t region_end = std::min(RoundUp(data_end, kSector), real_size);

  Layout layout;
  layout.real_size = real_size;
  layout.entry_count = count;
  layout.header.assign(head.begin(), head.begin() + ptrdiff_t(header_end));
  layout.segments.push_back({0, header_end, 0, 0, -1, 0, 0, Segment::kHeader});
  uint64_t cursor = header_end;
  for (size_t k = 0; k < items.size(); ++k) {
    const Item& item = items[k];
    const uint64_t next = k + 1 < items.size() ? items[k + 1].offset : region_end;
    const uint64_t span = next - item.offset;
    uint8_t* toc = layout.header.data() + 8 + item.index * 8;
    if (item.replacement < 0) {
      Put32(toc, uint32_t(cursor));
      if (span) layout.segments.push_back({cursor, span, item.offset, 0, -1, 0, 0, Segment::kOriginal});
      cursor += span;
      continue;
    }
    // A replaced entry must own its bytes alone: nothing else may start inside it.
    if (item.size > span || (k + 1 < items.size() && items[k + 1].offset < item.offset + std::max<uint64_t>(item.size, 1))) {
      *error = Fmt("entry %u shares its data with another entry", item.index);
      return false;
    }
    const Replacement& r = replacements[size_t(item.replacement)];
    const uint64_t slot = std::max(span, RoundUp(r.size, kSector));
    Put32(toc, uint32_t(cursor));
    Put32(toc + 4, uint32_t(std::max<uint64_t>(item.size, r.size)));
    layout.segments.push_back({cursor, slot, item.offset, span, r.pack_entry, item.index, r.size, Segment::kReplaced});
    cursor += slot;
    ++layout.replaced;
  }
  // The tail (name table and anything after the data), shifted as a block.
  const uint64_t shift = cursor - region_end;
  if (real_size > region_end) {
    layout.segments.push_back({cursor, real_size - region_end, region_end, 0, -1, 0, 0, Segment::kOriginal});
  }
  layout.virtual_size = cursor + (real_size - region_end);
  if (layout.virtual_size > UINT32_MAX) {
    *error = "virtual archive over 4 GiB";
    return false;
  }
  uint8_t* names = layout.header.data() + 8 + uint64_t(count) * 8;
  const uint64_t names_offset = Le32(names);
  if (names_offset >= region_end && names_offset < real_size) {
    Put32(names, uint32_t(names_offset + shift));
  } else if (names_offset != 0) {
    *error = Fmt("name table at 0x%llx is not after the data", (unsigned long long)names_offset);
    return false;
  }
  if (layout.replaced != replacements.size()) {
    *error = "a replacement targets an entry without data";
    return false;
  }
  *out = std::move(layout);
  return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------------------------------------------------
bool VirtualReader::Translate(const Segment& segment, uint64_t offset, uint64_t length) {
  if (&segment != latched_segment_ || offset != latched_next_) {
    latched_segment_ = &segment;
    latched_on_ = enabled_();
  }
  latched_next_ = offset + length;
  return latched_on_;
}

void VirtualReader::ReadOriginal(uint64_t offset, uint8_t* dst, size_t length) {
  size_t done = 0;
  while (done < length) {
    const size_t n = original_(offset + done, dst + done, length - done);
    if (n == 0) break;
    done += n;
  }
  if (done < length) std::memset(dst + done, 0, length - done);  // never past the real end; only on read errors
}

size_t VirtualReader::Read(uint64_t offset, uint8_t* dst, size_t length) {
  const Layout& layout = *layout_;
  if (offset >= layout.virtual_size || length == 0) return 0;
  length = size_t(std::min<uint64_t>(length, layout.virtual_size - offset));
  // The segment holding `offset`.
  auto it = std::upper_bound(layout.segments.begin(), layout.segments.end(), offset,
                             [](uint64_t v, const Segment& s) { return v < s.virtual_start; });
  size_t done = 0;
  for (--it; done < length; ++it) {
    const Segment& s = *it;
    const uint64_t at = offset + done;
    const uint64_t inside = at - s.virtual_start;
    const size_t n = size_t(std::min<uint64_t>(length - done, s.length - inside));
    uint8_t* out = dst + done;
    switch (s.kind) {
      case Segment::kHeader: std::memcpy(out, layout.header.data() + inside, n); break;
      case Segment::kOriginal: ReadOriginal(s.original_start + inside, out, n); break;
      case Segment::kReplaced: {
        Payload payload;
        uint64_t valid = s.original_length;  // bytes of real data in the slot, zeros after
        if (Translate(s, at, n) && (payload = translated_(s.pack_entry))) valid = payload->size();
        const size_t real = inside < valid ? size_t(std::min<uint64_t>(n, valid - inside)) : 0;
        if (real) {
          if (payload) {
            std::memcpy(out, payload->data() + inside, real);
          } else {
            ReadOriginal(s.original_start + inside, out, real);
          }
        }
        if (real < n) std::memset(out + real, 0, n - real);
        break;
      }
    }
    done += n;
  }
  return done;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pack
// ---------------------------------------------------------------------------------------------------------------------
Pack::~Pack() {
  if (file_) std::fclose(file_);
  if (dctx_) ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(dctx_));
}

bool Pack::Open(const std::filesystem::path& pack_path, const std::filesystem::path& us_dir, std::string* error) {
  std::error_code ec;
  pack_size_ = std::filesystem::file_size(pack_path, ec);
  if (ec) {
    *error = "cannot stat the pack";
    return false;
  }
  file_ = std::fopen(pack_path.string().c_str(), "rb");
  if (!file_) {
    *error = "cannot open the pack";
    return false;
  }
  uint8_t header[32];
  if (pack_size_ < sizeof(header) || !ReadAt(file_, 0, header, sizeof(header))) {
    *error = "pack too short";
    return false;
  }
  if (std::memcmp(header, "B3TR", 4) != 0 || Le32(header + 4) != 1) {
    *error = Fmt("not a B3TR v1 pack (magic %.4s, version %u)", reinterpret_cast<const char*>(header), Le32(header + 4));
    return false;
  }
  const uint32_t file_count = Le32(header + 8), entry_count = Le32(header + 12);
  const uint64_t file_table = Le32(header + 16), entry_table = Le32(header + 20);
  if (file_count == 0 || file_count > 64 || entry_count > 1u << 20 ||
      file_table + uint64_t(file_count) * 48 > pack_size_ || entry_table + uint64_t(entry_count) * 32 > pack_size_) {
    *error = Fmt("pack tables out of range (%u files, %u entries)", file_count, entry_count);
    return false;
  }
  std::vector<uint8_t> table(size_t(file_count) * 48);
  if (!ReadAt(file_, file_table, table.data(), table.size())) {
    *error = "cannot read the file table";
    return false;
  }
  for (uint32_t i = 0; i < file_count; ++i) {
    const uint8_t* p = table.data() + i * 48;
    PackFile f;
    f.name.assign(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 32));
    f.original_size = Le32(p + 32);
    f.afs_entry_count = Le32(p + 36);
    f.original_crc32 = Le32(p + 40);
    if (f.name.empty() || f.name.find_first_of("/\\") != std::string::npos || f.name == "." || f.name == "..") {
      *error = Fmt("bad file name in the pack (file %u)", i);
      return false;
    }
    f.path = us_dir / f.name;
    files_.push_back(std::move(f));
  }
  table.resize(size_t(entry_count) * 32);
  if (entry_count && !ReadAt(file_, entry_table, table.data(), table.size())) {
    *error = "cannot read the entry table";
    return false;
  }
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* p = table.data() + i * 32;
    PackEntry e;
    e.file_id = Le16(p);
    e.codec = Le16(p + 2);
    e.entry_index = Le32(p + 4);
    e.data_offset = Le64(p + 8);
    e.stored_size = Le32(p + 16);
    e.size = Le32(p + 20);
    e.crc32 = Le32(p + 24);
    if (e.file_id >= file_count || e.codec > 1 || e.size == 0 || e.data_offset + e.stored_size > pack_size_ ||
        (e.codec == 0 && e.stored_size != e.size)) {
      *error = Fmt("bad entry %u in the pack (file %u, AFS entry %u)", i, e.file_id, e.entry_index);
      return false;
    }
    if (i && (e.file_id < entries_.back().file_id ||
              (e.file_id == entries_.back().file_id && e.entry_index <= entries_.back().entry_index))) {
      *error = Fmt("pack entries not sorted at %u", i);
      return false;
    }
    entries_.push_back(e);
  }

  // Each archive: the user's file must be the one the pack was built against.
  for (uint32_t id = 0; id < file_count; ++id) {
    PackFile& f = files_[id];
    std::error_code size_ec;
    const uint64_t real_size = std::filesystem::file_size(f.path, size_ec);
    if (size_ec) {
      *error = Fmt("%s not found next to the game", f.name.c_str());
      return false;
    }
    if (real_size != f.original_size) {
      *error = Fmt("%s is %llu bytes, the pack expects %u (different game version or a modified file)", f.name.c_str(),
                   (unsigned long long)real_size, f.original_size);
      return false;
    }
    std::FILE* afs = std::fopen(f.path.string().c_str(), "rb");
    if (!afs) {
      *error = Fmt("cannot open %s", f.name.c_str());
      return false;
    }
    std::vector<uint8_t> head(size_t(std::min<uint64_t>(kProbe, real_size)));  // small (64 KiB) and resized below
    bool ok = ReadAt(afs, 0, head.data(), head.size());
    if (ok && Crc32(head) != f.original_crc32) {
      std::fclose(afs);
      *error = Fmt("%s: CRC32 of the first 64 KiB is %08X, the pack expects %08X", f.name.c_str(), Crc32(head),
                   f.original_crc32);
      return false;
    }
    if (ok && (head.size() < 8 || Le32(head.data() + 4) != f.afs_entry_count)) {
      std::fclose(afs);
      *error = Fmt("%s: AFS entry count differs from the pack", f.name.c_str());
      return false;
    }
    const uint64_t toc_end = 8 + uint64_t(f.afs_entry_count) * 8 + 8;
    if (ok && head.size() < toc_end && toc_end <= real_size) {
      head.resize(size_t(toc_end));
      ok = ReadAt(afs, 0, head.data(), head.size());
    }
    std::vector<Replacement> replacements;
    for (size_t e = 0; ok && e < entries_.size(); ++e) {
      const PackEntry& pe = entries_[e];
      if (pe.file_id != id) continue;
      // Optional cap (b3_traducao_ptbr_max_growth_mb): an entry whose slot would grow by more than this over the
      // original LZX entry is left out (stays English). Each load of such an entry costs the guest heap that much
      // extra, temporarily (read buffer, then the decompressed or copied data).
      if (max_growth_ && pe.entry_index < f.afs_entry_count && 12 + uint64_t(pe.entry_index) * 8 + 4 <= head.size()) {
        const uint64_t original = Le32(head.data() + 12 + pe.entry_index * 8);
        if (pe.size > original && pe.size - original > max_growth_) {
          dropped_.push_back(int32_t(e));
          continue;
        }
      }
      replacements.push_back({pe.entry_index, pe.size, int32_t(e)});
    }
    size_t need = 0;
    std::string why;
    while (ok && !BuildLayout(head, real_size, replacements, &f.layout, &why, &need)) {
      if (!need || need <= head.size() || need > real_size) {
        ok = false;
        break;
      }
      head.resize(need);
      ok = ReadAt(afs, 0, head.data(), head.size());
    }
    std::fclose(afs);
    if (!ok) {
      *error = Fmt("%s: %s", f.name.c_str(), why.empty() ? "read error" : why.c_str());
      return false;
    }
  }
  cache_.resize(entries_.size());
  failed_.assign(entries_.size(), 0);
  return true;
}

Payload Pack::Get(int32_t index) {
  if (index < 0 || size_t(index) >= entries_.size()) return nullptr;
  {
    std::lock_guard lock(cache_mutex_);
    if (failed_[size_t(index)]) return nullptr;
    Cached& c = cache_[size_t(index)];
    if (c.data) {
      c.use = ++use_clock_;
      ++stats_.hits;
      return c.data;
    }
  }
  const PackEntry& e = entries_[size_t(index)];
  std::shared_ptr<std::vector<uint8_t>> data;
  std::string why;
  try {
    // One decompression at a time: it also guards the FILE* and the reused context. Another reader of the same entry
    // waits here and then finds it in the cache.
    std::lock_guard io(io_mutex_);
    {
      std::lock_guard lock(cache_mutex_);
      if (cache_[size_t(index)].data) return cache_[size_t(index)].data;
    }
    // Not zeroed (the whole buffer is read over): a stored entry is read straight into its final vector.
    std::unique_ptr<uint8_t[]> stored;
    if (e.codec != 0) stored.reset(new uint8_t[e.stored_size ? e.stored_size : 1]);
    if (e.codec == 0) {
      data = std::make_shared<std::vector<uint8_t>>(e.stored_size);
      if (!ReadAt(file_, e.data_offset, data->data(), data->size())) {
        why = "read error";
        data.reset();
      }
    } else if (!ReadAt(file_, e.data_offset, stored.get(), e.stored_size)) {
      why = "read error";
    } else {
      if (!dctx_) dctx_ = ZSTD_createDCtx();
      data = std::make_shared<std::vector<uint8_t>>(e.size);
      const size_t n = dctx_ ? ZSTD_decompressDCtx(static_cast<ZSTD_DCtx*>(dctx_), data->data(), data->size(),
                                                   stored.get(), e.stored_size)
                             : size_t(-1);
      if (ZSTD_isError(n) || n != e.size) {
        why = dctx_ && ZSTD_isError(n) ? ZSTD_getErrorName(n) : "wrong size";
        data.reset();
      }
    }
    if (data && Crc32(*data) != e.crc32) {
      why = "CRC32 mismatch";
      data.reset();
    }
  } catch (const std::bad_alloc&) {
    why = "out of memory";
    data.reset();
  }
  std::lock_guard lock(cache_mutex_);
  if (!data) {
    failed_[size_t(index)] = 1;
    ++stats_.failures;
    PTBR_ERROR("[ptbr] cannot unpack %s entry %u (%s); the original entry is used",
               files_[e.file_id].name.c_str(), e.entry_index, why.c_str());
    return nullptr;
  }
  ++stats_.decompressed;
  cache_[size_t(index)] = {data, ++use_clock_};
  stats_.cache_bytes += data->size();
  // LRU: drop the oldest until under the bound, never the one just added.
  while (stats_.cache_bytes > cache_limit_) {
    size_t oldest = SIZE_MAX;
    for (size_t i = 0; i < cache_.size(); ++i) {
      if (cache_[i].data && int32_t(i) != index && (oldest == SIZE_MAX || cache_[i].use < cache_[oldest].use)) oldest = i;
    }
    if (oldest == SIZE_MAX) break;
    stats_.cache_bytes -= cache_[oldest].data->size();
    cache_[oldest] = {};
    ++stats_.evictions;
  }
  stats_.cache_peak = std::max(stats_.cache_peak, stats_.cache_bytes);
  return data;
}

Pack::Stats Pack::stats() const {
  std::lock_guard lock(cache_mutex_);
  return stats_;
}

// ---------------------------------------------------------------------------------------------------------------------
// The overlay in the port (ReXGlue VFS)
// ---------------------------------------------------------------------------------------------------------------------
#ifndef B3_PTBR_HOST_TEST
namespace {
using rex::X_STATUS;
std::atomic<Status> g_status{Status::kNotInstalled};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_active{false};
std::unique_ptr<Pack> g_pack;
std::atomic<uint32_t> g_translated_served{0};
std::vector<std::atomic<uint8_t>>* g_seen = nullptr;  // per pack entry: already logged

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The archive behind a host path, matched by "<dir>/<name>" with <dir> = us (case-insensitive).
const PackFile* FindFile(const std::filesystem::path& host_path) {
  if (!g_pack) return nullptr;
  const std::string name = Lower(host_path.filename().string());
  const std::string dir = Lower(host_path.parent_path().filename().string());
  if (dir != "us") return nullptr;
  for (const PackFile& f : g_pack->files()) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

Payload ServeTranslated(int32_t index) {
  Payload payload = g_pack->Get(index);
  if (payload && g_seen && !(*g_seen)[size_t(index)].exchange(1, std::memory_order_relaxed)) {
    const uint32_t count = g_translated_served.fetch_add(1, std::memory_order_relaxed) + 1;
    const PackEntry& e = g_pack->entries()[size_t(index)];
    // Low volume: the first few one by one, then one line every 100 distinct entries.
    if (count <= 8 || count % 100 == 0) {
      const auto s = g_pack->stats();
      REXLOG_INFO("[ptbr] translated entry served: {} #{} ({} bytes); {} distinct this session, cache {} KB "
                  "(peak {} KB, {} evictions)",
                  g_pack->files()[e.file_id].name, e.entry_index, e.size, count, s.cache_bytes >> 10,
                  s.cache_peak >> 10, s.evictions);
    }
  }
  return payload;
}

class OverlayFile final : public rex::filesystem::File {
 public:
  OverlayFile(uint32_t access, rex::filesystem::Entry* entry, rex::filesystem::File* inner, const Layout* layout)
      : File(access, entry),
        inner_(inner),
        reader_(
            layout,
            [this](uint64_t offset, uint8_t* dst, size_t n) -> size_t {
              size_t got = 0;
              if (XFAILED(inner_->ReadSync(std::span<uint8_t>(dst, n), size_t(offset), &got))) return 0;
              return got;
            },
            &ServeTranslated, [] { return g_enabled.load(std::memory_order_relaxed); }) {}
  ~OverlayFile() override {
    if (inner_) inner_->Destroy();
  }

  void Destroy() override { delete this; }

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override {
    if (!(file_access_ & (rex::filesystem::FileAccess::kGenericRead | rex::filesystem::FileAccess::kFileReadData))) {
      return X_STATUS_ACCESS_DENIED;
    }
    *out_bytes_read = reader_.Read(byte_offset, buffer.data(), buffer.size());
    return X_STATUS_SUCCESS;
  }
  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t* out_bytes_written) override {
    *out_bytes_written = 0;
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t) override { return X_STATUS_ACCESS_DENIED; }

 private:
  rex::filesystem::File* inner_;
  VirtualReader reader_;
};

class Overlay final : public rex::filesystem::FileOverlay {
 public:
  bool VirtualSize(const std::filesystem::path& host_path, uint64_t real_size, uint64_t* virtual_size) override {
    const PackFile* f = FindFile(host_path);
    if (!f || real_size != f->layout.real_size) return false;
    *virtual_size = f->layout.virtual_size;
    return true;
  }

  rex::filesystem::File* Wrap(const std::filesystem::path& host_path, uint32_t desired_access,
                              rex::filesystem::Entry* entry, rex::filesystem::File* inner) override {
    const PackFile* f = FindFile(host_path);
    if (!f || !entry || entry->size() != f->layout.virtual_size) return inner;
    return new OverlayFile(desired_access, entry, inner, &f->layout);
  }
};
Overlay g_overlay;
}  // namespace

Status Initialize(const std::filesystem::path& pack_path, const std::filesystem::path& us_dir, int max_growth_mb,
                  bool activate) {
  if (g_initialized.exchange(true)) return g_status.load();
  std::error_code ec;
  if (!std::filesystem::is_regular_file(pack_path, ec)) {
    REXLOG_INFO("[ptbr] translation pack not installed ({}): LANGUAGE stays ENGLISH", pack_path.string());
    g_status = Status::kNotInstalled;
    return g_status.load();
  }
  auto pack = std::make_unique<Pack>();
  if (max_growth_mb > 0) pack->set_max_growth(uint64_t(max_growth_mb) << 20);
  std::string error;
  if (!pack->Open(pack_path, us_dir, &error)) {
    REXLOG_WARN("[ptbr] translation pack {} is unusable, translation disabled: {}", pack_path.string(), error);
    g_status = Status::kUnavailable;
    return g_status.load();
  }
  uint64_t payload = 0;
  for (const auto& e : pack->entries()) payload += e.size;
  REXLOG_INFO("[ptbr] pack loaded: {} files, {} entries, {} MB translated payload, {} MB pack file",
              pack->files().size(), pack->entries().size(), payload >> 20,
              std::filesystem::file_size(pack_path, ec) >> 20);
  for (const auto& f : pack->files()) {
    if (!activate) break;
    REXLOG_INFO("[ptbr] virtual layout {}: {} AFS entries, {} replaced, {} -> {} bytes (+{} KB), {} segments",
                f.name, f.layout.entry_count, f.layout.replaced, f.layout.real_size, f.layout.virtual_size,
                (f.layout.virtual_size - f.layout.real_size) >> 10, f.layout.segments.size());
  }
  if (activate && !pack->dropped().empty()) {
    std::string list;
    for (int32_t i : pack->dropped()) {
      const PackEntry& e = pack->entries()[size_t(i)];
      if (list.size() < 400) list += Fmt(" %s#%u", pack->files()[e.file_id].name.c_str(), e.entry_index);
    }
    REXLOG_WARN("[ptbr] {} entries left out (stay English): they grow by more than {} MB over the original entry "
                "(b3_traducao_ptbr_max_growth_mb):{}",
                pack->dropped().size(), max_growth_mb, list);
  }
  g_status = Status::kReady;
  if (!activate) {
    // English players pay nothing: no overlay, no virtual sizes, the pack is closed again.
    REXLOG_INFO("[ptbr] pack valid; translation off at boot: files untouched (switch LANGUAGE and restart to use it)");
    return Status::kReady;
  }
  g_seen = new std::vector<std::atomic<uint8_t>>(pack->entries().size());
  g_pack = std::move(pack);
  rex::filesystem::SetFileOverlay(&g_overlay);
  g_active = true;
  REXLOG_INFO("[ptbr] pack valid; translation on at boot: virtual layout active, LANGUAGE switches live from the "
              "next screen");
  return Status::kReady;
}

Status GetStatus() { return g_status.load(); }

bool Active() { return g_active.load(); }

void SetEnabled(bool enabled) {
  enabled = enabled && g_active.load();
  if (g_enabled.exchange(enabled) == enabled) return;
  const auto s = g_pack ? g_pack->stats() : Pack::Stats{};
  REXLOG_INFO("[ptbr] LANGUAGE: {} (applies from the next screen the game loads); {} distinct translated entries "
              "served so far, cache {} KB",
              enabled ? "PORTUGUESE (BR)" : "ENGLISH", g_translated_served.load(), s.cache_bytes >> 10);
}

bool Enabled() { return g_enabled.load(); }
#endif

}  // namespace b3::ptbr
