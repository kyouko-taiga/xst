#pragma once

namespace xst {

struct TypeHeader;

/// A type identifier and a flag that is set if its instance is stored indirectly.
struct Field {

  /// An unowned pointer to a type header and a bit represented by the least significant bit.
  uintptr_t raw_value;

  /// Creates an instance with the given properties.
  inline Field(
    TypeHeader const* type, bool out_of_line = false
  ) : raw_value(reinterpret_cast<uintptr_t>(type) | out_of_line) {}

  /// Returns the type of the field.
  inline TypeHeader const* type() const {
    return reinterpret_cast<TypeHeader const*>(raw_value & ~1);
  }

  /// Returns `true` iff the field is stored out-of-line.
  inline bool out_of_line() const {
    return (raw_value & 1) != 0;
  }

};

}
