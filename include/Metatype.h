#pragma once

#include "Field.h"

#include <span>
#include <vector>

namespace xst {

/// Information about the runtime type of a value.
struct Metatype {
private:

  uintptr_t data;

  /// Returns a pointer to this instance's payload iff it is defined and stored stored out-of-line.
  /// Otherwise, returns `nullptr`.
  inline std::size_t* base_address() const {
    return ((data == 0) || (data & 1)) ? nullptr : reinterpret_cast<std::size_t*>(data & ~0b11);
  }

public:

  Metatype() : data(0) {};

  Metatype(
    std::size_t size, std::size_t alignment, bool is_trivial,
    std::vector<Field>&& fields,
    std::vector<std::size_t>&& offsets
  );

  Metatype(Metatype const&) = delete;

  Metatype& operator=(Metatype const&)= delete;

  Metatype(Metatype&& other) : data(other.data) { other.data = 0; }

  Metatype& operator=(Metatype&& other) {
    delete base_address();
    this->data = other.data;
    other.data = 0;
    return *this;
  }

  ~Metatype() {
    delete base_address();
  }

  /// Returns `true` if this instance is defined.
  inline bool defined() const {
    return data != 0;
  }

  /// Returns `true` iff none of the describe type does not contain out-of-line storage.
  ///
  /// - Requires: `this` is defined.
  inline bool is_trivial() const {
    return (data & 0b10) != 0;
  }

  /// Returns the size of the described type.
  ///
  /// - Requires: `this` is defined.
  inline std::size_t size() const {
    auto base = base_address();
    if (base == nullptr) {
      return static_cast<std::size_t>((data >> 32) & 0xffff);
    } else {
      return base[0];
    }
  }

  /// Returns the alignment of the described type.
  ///
  /// - Requires: `this` is defined.
  inline std::size_t alignment() const {
    auto base = base_address();
    if (base == nullptr) {
      return static_cast<std::size_t>((data >> 16) & 0xffff);
    } else {
      return base[1];
    }
  }

  /// Returns the fields of the described type, if any.
  ///
  /// - Requires: `this` is defined.
  std::span<Field const> fields() const {
    auto base = base_address();
    if (base == nullptr) {
      return std::span<Field const>{};
    } else {
      auto s = base[2];
      auto b = static_cast<Field const*>(static_cast<void*>(base + 3));
      return std::span<Field const>{b, s};
    }
  }

  /// Returns the offsets of the described type, if any.
  ///
  /// - Requires: `this` is defined.
  std::span<std::size_t> offsets() const {
    auto base = base_address();
    if (base == nullptr) {
      return std::span<std::size_t>{};
    } else {
      auto s = base[2];
      auto b = base + 3 + s;
      return std::span<std::size_t>{b, s};
    }
  }

};

}
