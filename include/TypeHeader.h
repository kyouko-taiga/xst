#pragma once

#include "Utilities.h"

#include <sstream>
#include <string>
#include <vector>

namespace xst {

struct TypeHeader;
struct TypeStore;

/// The information necessary to uniquely identify atype.
struct TypeHeader {

  /// Destorys `this`.
  virtual ~TypeHeader() = default;

  /// Returns a hash of the salient part of `this`.
  virtual std::size_t hash_value() const = 0;

  /// Returns `true` iff `this` is equal to the given identifier.
  virtual bool equal_to(TypeHeader const&) const = 0;

  /// Returns a textual description of the type.
  virtual std::string description() const = 0;

  /// Returns `true` iff `this` is equal to `other`.
  bool operator==(TypeHeader const& other) const {
    return this->equal_to(other);
  }

private:

  // Note: The following methods are notionally methods of the type store and are no meant to be
  // called from anywhere else. They are defined here to enable the default dynamic dispatch
  // mechanism of virtual calls.

  friend TypeStore;

  /// Implements `TypeStore::copy_initialize` for the described type.
  virtual void copy_initialize(void*, void*, TypeStore const&) const = 0;

  /// Implements `TypeStore::deinitialize` for the described type.
  virtual void deinitialize(void*, TypeStore const&) const = 0;

  /// Implements `TypeStore::dump_instance` for the described type.
  virtual void dump_instance(std::ostream&, void*, TypeStore const&) const = 0;

};

/// The header of a built-in type.
struct BuiltinHeader final : public TypeHeader {

  /// The internal representation of a built-in identifier.
  enum Value : uint8_t {
    boolean,
    i32,
    i64,
    str,
  };

  /// The raw value of this identifier.
  Value raw_value;

  /// Creates an instance with the given raw value.
  constexpr BuiltinHeader(Value raw_value) : raw_value(raw_value) {}

  /// Returns the size of an instance of the type.
  constexpr std::size_t size() const {
    switch (raw_value) {
      case BuiltinHeader::boolean:
        return sizeof(bool);
      case BuiltinHeader::i32:
        return sizeof(int32_t);
      case BuiltinHeader::i64:
        return sizeof(int64_t);
      case BuiltinHeader::str:
        return sizeof(char const*);
    }
  }

  /// Returns the alignment the type.
  constexpr std::size_t alignment() const {
    switch (raw_value) {
      case BuiltinHeader::boolean:
        return alignof(bool);
      case BuiltinHeader::i32:
        return alignof(int32_t);
      case BuiltinHeader::i64:
        return alignof(int64_t);
      case BuiltinHeader::str:
        return alignof(char const*);
    }
  }

  constexpr std::size_t hash_value() const override {
    return static_cast<std::size_t>(raw_value);
  }

  constexpr bool equal_to(TypeHeader const& other) const override {
    auto const* that = dynamic_cast<BuiltinHeader const*>(&other);
    if (that != nullptr) {
      return this->raw_value == that->raw_value;
    } else {
      return false;
    }
  }

  constexpr std::string description() const override {
    switch (raw_value) {
      case Value::boolean: return "Bool";
      case Value::i32: return "Int32";
      case Value::i64: return "Int64";
      case Value::str: return "String";
    }
  }

private:

  void copy_initialize(void*, void*, TypeStore const&) const override;

  void deinitialize(void*, TypeStore const&) const override;

  void dump_instance(std::ostream&, void*, TypeStore const&) const override;

};

/// Common implementation of nominal product and sum types.
struct CompositeHeader : public TypeHeader {

  /// The name of the type.
  const char* name;

  /// The type arguments of the type.
  const std::vector<TypeHeader const*> arguments;

  /// Creates an instance with the given properties.
  constexpr CompositeHeader(
    const char* name, std::initializer_list<TypeHeader const*> arguments
  ) : name(std::move(name)), arguments(arguments) {}

  constexpr std::size_t hash_value() const override {
    Hasher h;
    h.combine(name);
    h.combine(arguments.begin(), arguments.end());
    return h.finalize();
  }

  std::string description() const override {
    std::stringstream o;
    o << name;
    if (!arguments.empty()) {
      o << "<";
      auto f = true;
      for (auto a : arguments) {
        if (f) { f = false; } else { o << ", "; }
        o << a->description();
      }
      o << ">";
    }
    return o.str();
  }

};

/// The header of a product type.
struct StructHeader final : public CompositeHeader {

  /// Creates an instance with the given properties.
  constexpr StructHeader(
    const char* name, std::initializer_list<TypeHeader const*> arguments
  ) : CompositeHeader(name, arguments) {}

  constexpr bool equal_to(TypeHeader const& other) const override {
    auto const* that = dynamic_cast<StructHeader const*>(&other);
    if (that != nullptr) {
      return (this->name == that->name) && (this->arguments == that->arguments);
    } else {
      return false;
    }
  }

private:

  void copy_initialize(void*, void*, TypeStore const&) const override;

  void deinitialize(void*, TypeStore const&) const override;

  void dump_instance(std::ostream&, void*, TypeStore const&) const override;

};

/// The header of a sum type.
struct EnumHeader final : public CompositeHeader {

  /// Creates an instance with the given properties.
  constexpr EnumHeader(
     const char* name, std::initializer_list<TypeHeader const*> arguments
   ) : CompositeHeader(name, arguments) {}

  constexpr bool equal_to(TypeHeader const& other) const override {
    auto const* that = dynamic_cast<EnumHeader const*>(&other);
    if (that != nullptr) {
      return (this->name == that->name) && (this->arguments == that->arguments);
    } else {
      return false;
    }
  }

private:

  void copy_initialize(void*, void*, TypeStore const&) const override;

  void deinitialize(void*, TypeStore const&) const override;

  void dump_instance(std::ostream&, void*, TypeStore const&) const override;

};

}

template<>
struct std::hash<xst::TypeHeader> {

  std::size_t operator()(xst::TypeHeader const& i) const {
    return i.hash_value();
  }

};
