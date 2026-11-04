#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ButtonLink {
constexpr size_t WIRE_SIZE = 16;
constexpr uint32_t LINK_TIMEOUT_MS = 2500;
enum class Kind : uint8_t { State = 1, Ack = 2 };
enum class Result : uint8_t { Rejected, Duplicate, Updated };

struct Packet {
  Kind kind;
  uint8_t from;
  uint8_t to;
  bool pressed;
  uint32_t session;
  uint32_t sequence;
};

inline bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

inline void put32(uint8_t *out, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) out[i] = uint8_t(value >> (8 * i));
}

inline uint32_t get32(const uint8_t *in) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
  return value;
}

inline void encode(const Packet &packet, uint8_t *out) {
  out[0] = 0xC7; out[1] = 0x32; out[2] = 1;
  out[3] = uint8_t(packet.kind);
  out[4] = packet.from; out[5] = packet.to;
  out[6] = packet.pressed ? 1 : 0; out[7] = 0;
  put32(out + 8, packet.session);
  put32(out + 12, packet.sequence);
}

inline bool decode(const uint8_t *in, size_t length, Packet &packet) {
  if (length != WIRE_SIZE || in[0] != 0xC7 || in[1] != 0x32 || in[2] != 1 ||
      (in[3] != uint8_t(Kind::State) && in[3] != uint8_t(Kind::Ack)) ||
      in[4] < 1 || in[4] > 2 || in[5] < 1 || in[5] > 2 ||
      in[4] == in[5] || in[6] > 1 || in[7] != 0) return false;
  packet = {Kind(in[3]), in[4], in[5], in[6] != 0, get32(in + 8), get32(in + 12)};
  return true;
}

// Explicit states make duplicate packets harmless. Sequence checks reject old
// state packets; a new boot session permits the peer to restart its counter.
struct RemoteState {
  bool seen = false;
  bool pressed = false;
  uint32_t session = 0;
  uint32_t sequence = 0;
  uint32_t lastFreshAt = 0;

  Result accept(const Packet &packet, uint32_t now) {
    if (packet.kind != Kind::State) return Result::Rejected;
    if (seen && packet.session == session) {
      if (packet.sequence == sequence) return Result::Duplicate;
      if (static_cast<int32_t>(packet.sequence - sequence) <= 0) return Result::Rejected;
    }
    seen = true;
    pressed = packet.pressed;
    session = packet.session;
    sequence = packet.sequence;
    lastFreshAt = now;
    return Result::Updated;
  }

  bool expire(uint32_t now) {
    if (seen && pressed && uint32_t(now - lastFreshAt) >= LINK_TIMEOUT_MS) {
      pressed = false;
      return true;
    }
    return false;
  }
};
}  // namespace ButtonLink
