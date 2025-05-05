#pragma once

#include "Utilities.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace xst {

/// A tag identifying the type of a header.
enum TypeHeaderTag : uint8_t {
  none_tag,
  builtin_tag,
  struct_tag,
  enum_tag,
};

template<TypeHeaderTag tag>
struct CompositeHeader;

struct BuiltinHeader;
using EnumHeader = CompositeHeader<enum_tag>;
using StructHeader = CompositeHeader<struct_tag>;

/// The information necessary to uniquely identify a type at runtime.
struct TypeHeader {
private:

  /// The raw value of this instance.
  std::uintptr_t raw_value;

public:

  /// A tag identifying the type of a header.
  using Tag = TypeHeaderTag;

  TypeHeader(TypeHeader const&) = delete;
  TypeHeader& operator=(TypeHeader const&) = delete;

  TypeHeader(TypeHeader&& other) : raw_value(other.raw_value) {
    other.raw_value = 0;
  }

  template<typename Header>
  TypeHeader(Header&& other) : raw_value(other.raw_value) {
    other.raw_value = 0;
  }

  TypeHeader& operator=(TypeHeader&& other) {
    raw_value = other.raw_value;
    other.raw_value = 0;
    return *this;
  }

  ~TypeHeader();

  /// Returns the raw value of this instance.
  inline constexpr std::uintptr_t raw() const {
    return raw_value;
  }

  /// Returns the tag of this instance, which identifies the contents of this header.
  inline constexpr Tag tag() const {
    return static_cast<Tag>(raw_value & 7);
  }

  /// Returns `this` if it was widened from an instance of `T` or `nullptr` otherwise.
  template<typename T>
  inline constexpr T const* as() const {
    return tag() == T::header_tag()
      ? static_cast<T const*>(static_cast<void const*>(this))
      : nullptr;
  }

  /// Returns a hash of the notional value of `this`.
  std::size_t hash_value() const;

  /// Returns `true` iff `this` is equal to `other`.
  bool equal_to(TypeHeader const& other) const;

  /// Returns `true` iff `this` is equal to `other`.
  inline bool operator==(TypeHeader const& other) const {
    return equal_to(other);
  }

  /// Returns a description of the type identified by this instance.
  std::string description() const;

  /// Returns `tag` as a mask.
  static inline constexpr std::uintptr_t mask(Tag tag) {
    return static_cast<std::uintptr_t>(tag);
  }

};

/// The header of a built-in type.
struct BuiltinHeader {
private:

  friend TypeHeader;

  /// The raw value of this instance.
  std::uintptr_t raw_value;

public:

  /// Returns the type header tag associated of this definition.
  static inline constexpr TypeHeader::Tag header_tag() { return builtin_tag; }

  /// A tag identifying a built-in type.
  enum Tag : uint8_t {
    boolean, i32, i64, str,
  };

  /// Returns the tag of the type identified by this header.
  inline constexpr Tag tag() const {
    return static_cast<Tag>((raw_value >> 8) & 0xff);
  }

  /// Creates an instance identifying the given built-in type.
  constexpr BuiltinHeader(
    Tag tag
  ) : raw_value(
    static_cast<std::uintptr_t>(tag) << 8 | TypeHeader::mask(builtin_tag)
  ) {}

  /// Returns a hash of the notional value of `this`.
  inline constexpr std::size_t hash_value() const {
    return static_cast<std::size_t>(raw_value);
  }

  /// Returns `true` iff `this` is equal to `other`.
  constexpr bool operator==(BuiltinHeader const& other) const = default;

  /// Returns `true` iff `this` is equal to `other`.
  constexpr inline bool equal_to(TypeHeader const& other) const {
    auto that = other.as<BuiltinHeader>();
    return (that != nullptr) && (*this == *that);
  }

  /// Returns a description of the type identified by this instance.
  constexpr std::string description() const {
    switch (tag()) {
      case xst::BuiltinHeader::boolean:
        return "Bool";
      case xst::BuiltinHeader::i32:
        return "Int32";
      case xst::BuiltinHeader::i64:
        return "Int64";
      case xst::BuiltinHeader::str:
        return "String";
    }
  }

  /// Returns a pointer to `this` as a polymorphic type header.
  inline constexpr TypeHeader const* widened() const {
    return static_cast<TypeHeader const*>(static_cast<void const*>(this));
  }

};

template<TypeHeader::Tag tag>
struct CompositeHeader {
private:

  friend TypeHeader;

  /// The raw value of this instance.
  std::uintptr_t raw_value;

  /// Returns the base address of this instance's representation, or `nullptr` if `this` is moved.
  inline std::uintptr_t* representation() const {
    return reinterpret_cast<std::uintptr_t*>(raw_value & ~7);
  }

public:

  /// Returns the type header tag associated of this definition.
  static inline constexpr TypeHeader::Tag header_tag() { return tag; }

  /// Creates an instance with the given properties.
  CompositeHeader(
    const char* name, std::initializer_list<TypeHeader const*> arguments
  ) {
    constexpr auto a = std::max<std::size_t>(alignof(std::uintptr_t), 8);
    auto p = new (std::align_val_t(a)) std::uintptr_t[2 + arguments.size()];
    auto q = p;

    // Store the name, argument count, and argument values.
    *(q++) = reinterpret_cast<std::uintptr_t>(name);
    *(q++) = static_cast<std::uintptr_t>(arguments.size());
    for (auto a : arguments) { *(q++) = a->raw(); }

    // Store the tag.
    raw_value = reinterpret_cast<std::uintptr_t>(p) | TypeHeader::mask(struct_tag);
  }

  CompositeHeader(CompositeHeader const&) = delete;
  CompositeHeader& operator=(CompositeHeader const&) = delete;

  CompositeHeader(CompositeHeader&& other) : raw_value(other.raw_value) {
    other.raw_value = 0;
  }

  CompositeHeader& operator=(CompositeHeader&& other) {
    raw_value = other.raw_value;
    other.raw_value = 0;
    return *this;
  }

  ~CompositeHeader() {
    delete representation();
  }

  /// Returns the name of the type.
  const char* name() const {
    auto p = representation();
    if (p == nullptr) {
      return nullptr;
    } else {
      auto n = reinterpret_cast<const char*>(p[0]);
      return n;
    }
  }

  /// Returns the number of type arguments.
  std::size_t arguments_size() const {
    auto p = representation();
    if (p == nullptr) {
      return 0;
    } else {
      auto s = static_cast<std::size_t>(p[1]);
      return s;
    }
  }

  /// Returns an iterator to the beginning of type arguments.
  TypeHeader const* arguments_begin() const {
    auto p = representation();
    if (p == nullptr) {
      return nullptr;
    } else {
      auto a = reinterpret_cast<TypeHeader*>(p + 2);
      return a;
    }
  }

  /// Returns an iterator to the end of the type arguments.
  TypeHeader const* arguments_end() const {
    auto p = representation();
    if (p == nullptr) {
      return nullptr;
    } else {
      auto s = static_cast<std::size_t>(p[1]);
      auto a = reinterpret_cast<TypeHeader*>(p + 2);
      return a + s;
    }
  }

  /// Returns a hash of the notional value of `this`.
  constexpr std::size_t hash_value() const {
    Hasher h;
    h.combine(name());
    h.combine(arguments_begin(), arguments_end());
    return h.finalize();
  }

  /// Returns `true` iff `this` is equal to `other`.
  constexpr inline bool equal_to(TypeHeader const& other) const {
    auto that = other.as<CompositeHeader<tag>>();
    return (that != nullptr) && (*this == *that);
  }

  /// Returns `true` iff `this` is equal to `other`.
  bool operator==(CompositeHeader const& other) const {
    if (this->raw_value == other.raw_value) {
      return true;
    } else if (this->name() == other.name()) {
      auto lb = this->arguments_begin();
      auto le = this->arguments_end();
      auto rb = other.arguments_begin();
      auto re = other.arguments_end();
      return std::equal(lb, le, rb, re);
    } else {
      return false;
    }
  }

  /// Returns a description of the type identified by this instance.
  std::string description() const {
    std::stringstream s;
    s << name();
    auto b = arguments_begin();
    auto e = arguments_end();

    if (b != e) {
      s << "<";
      auto i = b;
      while (i != e) {
        if (i != b) { s << ", "; }
        s << i->description();
        ++i;
      }
      s << ">";
    }

    return s.str();
  }

  /// Returns a pointer to `this` as a polymorphic type header.
  inline constexpr TypeHeader const* widened() const {
    return static_cast<TypeHeader const*>(static_cast<void const*>(this));
  }

};

}

template<>
struct std::hash<xst::TypeHeader> {

  std::size_t operator()(xst::TypeHeader const& h) const {
    return h.hash_value();
  }

};
