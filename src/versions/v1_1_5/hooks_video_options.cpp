#include "patches_internal.h"

#include <cstring>

#include "shim/game_profile.h"
#include "shim/log.h"
#include "shim/memory.h"
#include "shim/object_access.h"
#include "shim/patch.h"
#include "shim/runtime_config.h"

namespace shim::versions::v115::detail {
namespace {

constexpr DWORD kLookupOptionRva = 0x000D6C00;
constexpr DWORD kOptionEndSentinelRva = 0x014101FC;
constexpr DWORD kConstructIntVectorRva = 0x0009AA20;
constexpr DWORD kGameDeleteRva = 0x00E1F955;

// ServerPlayer::setClientChunkRadius(unsigned int) in Minecraft 1.1.5 clamps
// the requested radius to at least five chunks before updating the two
// PlayerChunkSource instances and replying with ChunkRadiusUpdatedPacket.
//
// C40999: mov eax,[edi+1294h]       ; server maximum
// C4099F: mov ecx,[ebp+08h]         ; requested radius
// C409A2: cmp ecx,eax
// C409A4: ja  C409B0
// C409A6: mov eax,5                 ; stock minimum (patch immediate only)
// C409AB: cmp ecx,eax
// C409AD: cmova eax,ecx
// C409B0: mov [edi+12BCh],eax       ; accepted client radius
// C409B6: add eax,5                 ; publisher/load margin -- KEEP STOCK
constexpr DWORD kServerChunkRadiusClampRva = 0x00840999;
constexpr DWORD kServerChunkRadiusMinimumImmediateRva = 0x008409A7;
constexpr DWORD kServerChunkRadiusClampTailRva = 0x008409AB;

constexpr unsigned char kServerChunkRadiusClampPrefix[] = {
    0x8B, 0x87, 0x94, 0x12, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x3B, 0xC8,
    0x77, 0x0A, 0xB8};

constexpr unsigned char kServerChunkRadiusClampTail[] = {
    0x3B, 0xC8, 0x0F, 0x47, 0xC1,
    0x89, 0x87, 0xBC, 0x12, 0x00, 0x00,
    0x83, 0xC0, 0x05,
    0x89, 0x87, 0x38, 0x12, 0x00, 0x00};

constexpr int kRenderDistanceOptionId = 0x14;
constexpr ptrdiff_t kOptionMinimumOffset = 0x68;
constexpr unsigned int kMaximumOriginalValues = 64;
constexpr unsigned int kMaximumMergedValues = kMaximumOriginalValues + 4;

struct GameIntVector {
  int* begin;
  int* end;
  int* capacity;
};
static_assert(sizeof(GameIntVector) == 12,
              "MSVC x86 vector<int> layout must stay 12 bytes");

using LookupOptionFn = void(SHIM_COM*)(void** result, int* option_id);
using ConstructIntVectorFn = bool(__thiscall*)(GameIntVector* vector,
                                                unsigned int count);
using GameDeleteFn = void(__cdecl*)(void* memory);

bool g_render_distance_values_extended = false;

bool PatchServerChunkRadiusMinimum() noexcept {
  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (base == 0) {
    return false;
  }

  const void* const prefix =
      reinterpret_cast<const void*>(base + kServerChunkRadiusClampRva);
  const void* const tail =
      reinterpret_cast<const void*>(base + kServerChunkRadiusClampTailRva);
  auto* const immediate = reinterpret_cast<DWORD*>(
      base + kServerChunkRadiusMinimumImmediateRva);

  // Validate both sides of the immediate. In particular the tail includes the
  // stock `accepted + 5` publisher margin so this patch can never silently
  // drift onto a different 1.1.5 revision or modify that separate radius.
  if (!hooks::MatchesSignature(prefix, kServerChunkRadiusClampPrefix,
                               sizeof(kServerChunkRadiusClampPrefix)) ||
      !hooks::MatchesSignature(tail, kServerChunkRadiusClampTail,
                               sizeof(kServerChunkRadiusClampTail)) ||
      !memory::IsReadable(immediate, sizeof(*immediate))) {
    log::Write(
        "1.1.5 server chunk-radius minimum patch refused: signature mismatch");
    return false;
  }

  DWORD desired_minimum = runtime_config::Get().min_render_distance;
  if (desired_minimum < 1u) {
    desired_minimum = 1u;
  } else if (desired_minimum > 4u) {
    desired_minimum = 4u;
  }

  const DWORD current_minimum = *immediate;
  if (current_minimum == desired_minimum) {
    return true;
  }
  if (current_minimum != 5u) {
    log::Writef(
        "1.1.5 server chunk-radius minimum patch refused: current=%lu",
        static_cast<unsigned long>(current_minimum));
    return false;
  }

  if (!hooks::WriteCode(immediate, &desired_minimum,
                        sizeof(desired_minimum))) {
    log::Write("1.1.5 server chunk-radius minimum patch write failed");
    return false;
  }

  log::Writef("1.1.5 server chunk-radius minimum: %lu -> %lu chunks",
              static_cast<unsigned long>(current_minimum),
              static_cast<unsigned long>(desired_minimum));
  return true;
}

void* ResolveRenderDistanceOption() noexcept {
  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  if (base == 0) {
    return nullptr;
  }

  auto lookup = reinterpret_cast<LookupOptionFn>(base + kLookupOptionRva);
  void* const sentinel_address =
      reinterpret_cast<void*>(base + kOptionEndSentinelRva);
  if (!memory::IsExecutable(reinterpret_cast<const void*>(lookup)) ||
      !memory::IsReadable(sentinel_address, sizeof(void*))) {
    return nullptr;
  }

  void* const end = *reinterpret_cast<void**>(sentinel_address);
  // The lookup routine dereferences the tree sentinel immediately. During
  // early startup that global is still null, so retry on a later frame.
  if (end == nullptr || !memory::IsReadable(end, 0x14)) {
    return nullptr;
  }

  void* node = nullptr;
  int option_id = kRenderDistanceOptionId;
  lookup(&node, &option_id);

  if (node == nullptr || node == end ||
      !memory::IsReadable(object_access::Field(node, 0x14), sizeof(void*))) {
    return nullptr;
  }

  return *reinterpret_cast<void**>(object_access::Field(node, 0x14));
}

bool Contains(const int* values, unsigned int count, int value) noexcept {
  for (unsigned int i = 0; i < count; ++i) {
    if (values[i] == value) {
      return true;
    }
  }
  return false;
}

void SortAscending(int* values, unsigned int count) noexcept {
  for (unsigned int i = 1; i < count; ++i) {
    const int value = values[i];
    unsigned int j = i;
    while (j != 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
}

bool ExtendRenderDistanceValues(void* option) noexcept {
  if (option == nullptr ||
      !memory::IsReadable(object_access::Field(option, kOptionMinimumOffset),
                          sizeof(int)) ||
      !memory::IsWritable(object_access::Field(option, kOptionMinimumOffset),
                          sizeof(int)) ||
      !memory::IsReadable(object_access::Field(option, 0x74),
                          sizeof(GameIntVector)) ||
      !memory::IsWritable(object_access::Field(option, 0x74),
                          sizeof(GameIntVector))) {
    return false;
  }

  auto* const vector =
      reinterpret_cast<GameIntVector*>(object_access::Field(option, 0x74));
  const GameIntVector original = *vector;
  if (original.begin == nullptr || original.end == nullptr ||
      original.capacity == nullptr || original.end < original.begin ||
      original.capacity < original.end) {
    return false;
  }

  const ptrdiff_t byte_count = reinterpret_cast<const unsigned char*>(original.end) -
                               reinterpret_cast<const unsigned char*>(original.begin);
  if (byte_count <= 0 || (byte_count % static_cast<ptrdiff_t>(sizeof(int))) != 0) {
    return false;
  }

  const unsigned int original_count =
      static_cast<unsigned int>(byte_count / sizeof(int));
  if (original_count > kMaximumOriginalValues ||
      !memory::IsReadable(original.begin, original_count * sizeof(int))) {
    return false;
  }

  int merged[kMaximumMergedValues] = {};
  unsigned int merged_count = 0;
  for (unsigned int i = 0; i < original_count; ++i) {
    if (!Contains(merged, merged_count, original.begin[i])) {
      merged[merged_count++] = original.begin[i];
    }
  }

  int minimum_chunks = static_cast<int>(runtime_config::Get().min_render_distance);
  if (minimum_chunks < 1) {
    minimum_chunks = 1;
  } else if (minimum_chunks > 4) {
    minimum_chunks = 4;
  }

  bool changed = false;
  for (int chunks = minimum_chunks; chunks <= 4; ++chunks) {
    if (!Contains(merged, merged_count, chunks)) {
      if (merged_count >= kMaximumMergedValues) {
        return false;
      }
      merged[merged_count++] = chunks;
      changed = true;
    }
  }

  SortAscending(merged, merged_count);
  auto* const cached_minimum = reinterpret_cast<int*>(
      object_access::Field(option, kOptionMinimumOffset));
  if (!changed) {
    *cached_minimum = merged[0];
    return true;
  }

  const uintptr_t base = reinterpret_cast<uintptr_t>(game::Base());
  auto construct =
      reinterpret_cast<ConstructIntVectorFn>(base + kConstructIntVectorRva);
  auto game_delete = reinterpret_cast<GameDeleteFn>(base + kGameDeleteRva);
  if (base == 0 ||
      !memory::IsExecutable(reinterpret_cast<const void*>(construct)) ||
      !memory::IsExecutable(reinterpret_cast<const void*>(game_delete))) {
    return false;
  }

  GameIntVector replacement = {};
  if (!construct(&replacement, merged_count) || replacement.begin == nullptr ||
      replacement.capacity == nullptr ||
      replacement.capacity < replacement.begin + merged_count ||
      !memory::IsWritable(replacement.begin, merged_count * sizeof(int))) {
    if (replacement.begin != nullptr) {
      game_delete(replacement.begin);
    }
    return false;
  }

  memcpy(replacement.begin, merged, merged_count * sizeof(int));
  replacement.end = replacement.begin + merged_count;

  *vector = replacement;
  *cached_minimum = merged[0];
  game_delete(original.begin);
  return true;
}

}  // namespace

bool InstallVideoOptionHooks() noexcept {
  if (game::Current() != game::Version::kV1_1_5) {
    return true;
  }

  const bool server_radius_ok = PatchServerChunkRadiusMinimum();

  // The option registry is initialized later by the game. RunFrame retries the
  // one-time override until the render-distance option and its vector exist.
  g_render_distance_values_extended = false;
  log::Write("1.1.5 render-distance option override armed");
  return server_radius_ok;
}

void EnsureVideoOptionOverrides() noexcept {
  if (g_render_distance_values_extended ||
      game::Current() != game::Version::kV1_1_5) {
    return;
  }

  void* const option = ResolveRenderDistanceOption();
  if (option == nullptr || !ExtendRenderDistanceValues(option)) {
    return;
  }

  g_render_distance_values_extended = true;
  log::Write("1.1.5 render-distance discrete values extended");
}

}  // namespace shim::versions::v115::detail
