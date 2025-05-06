#pragma once

#include "TypeHeader.h"
#include "Utilities.h"

#include <span>
#include <unordered_map>

namespace xst {

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

template<typename Header>
struct MetatypeConstructor {
  Metatype operator()(Header const*, TypeStore&) {
    return Metatype{};
  }
};

struct TypeStore {
private:

  /// An array containing the type ifentifiers allocated in this store.
  std::vector<std::unique_ptr<TypeHeader>> identifiers;

  /// A table from a type identifier to its corresponding metatype.
  std::unordered_map<DereferencingKey<TypeHeader>, Metatype> metatype;

  /// Returns `t`'s metatype.
  ///
  /// - Requires: `t` has been declared and never explicitly defined in `this`.
  Metatype& get_metatype(TypeHeader const* t);

public:

  /// Creates an empty instance.
  TypeStore() = default;

  /// Returns a pointer to the unique instance equal to `identifier` in this store.
  template<typename T, typename M = MetatypeConstructor<T>>
  T const* declare(T&& identifier) {
    auto entry = metatype.find(DereferencingKey<TypeHeader>{&identifier});

    // The identifier is already known.
    if (entry != metatype.end()) {
      return static_cast<T const*>(entry->first.value);
    }

    // The identifier is unknown; intern it.
    else {
      identifiers.emplace_back(std::make_unique<T>(std::move(identifier)));
      auto const* p = identifiers.back().get();
      auto const* q = static_cast<T const*>(p);
      metatype.emplace(std::make_pair(DereferencingKey{p}, M{}(q, *this)));
      return q;
    }
  }

  /// Returns a pointer to the unique instance identifying `tag` in this store.
  inline BuiltinHeader const* declare(BuiltinHeader::Value tag) {
    return declare(BuiltinHeader{tag});
  }

  /// Assigns a metatype definition to `type`.
  ///
  /// - Requires: `type` has been declared and never explicitly defined in `this`.
  Metatype const& define(StructHeader const* type, std::vector<Field>&&);

  /// Assigns a metatype definition to `type`.
  ///
  /// - Requires: `type` has been declared and never explicitly defined in `this`.
  Metatype const& define(EnumHeader const* type, std::vector<Field>&&);

  /// Accesses the metatype associated to `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  Metatype const& operator[](TypeHeader const* type) const;

  /// Returns `true` iff `h` has been declared and defined in `this`.
  inline bool defined(TypeHeader const* h) const {
    auto entry = metatype.find(DereferencingKey<TypeHeader>{h});
    return (entry != metatype.end()) && entry->second.defined();
  }

  /// Returns `true` iff instances of `type` do not involve out-of-line storage.
  ///
  /// - Requires `type` has been declared and defined in `this`.
  inline bool is_trivial(TypeHeader const* type) const {
    return (*this)[type].is_trivial();
  }

  /// Returns `true` iff `f` does not involve out-of-line storage.
  inline bool is_trivial(Field const& f) const {
    return !f.out_of_line() && is_trivial(f.type());
  }

  /// Returns `true` iff none of the given fields involves out-of-line storage.
  inline bool all_trivial(std::span<Field const> const& fields) const {
    return std::all_of(fields.begin(), fields.end(), [&](auto const& f) {
      return is_trivial(f);
    });
  }

  /// Returns the size of an instance of `t`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  inline std::size_t size(TypeHeader const* type) const {
    return (*this)[type].size();
  }

  /// Implements `size` for built-in types.
  constexpr std::size_t size(BuiltinHeader const* h) const {
    switch (h->raw_value) {
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

  /// Returns the size of `field`.
  ///
  /// - Requires: the type of `field` has been declared and defined in `this`.
  inline std::size_t size(Field const& field) const {
    return field.out_of_line() ? sizeof(void*) : size(field.type());
  }

  /// Returns the alignment of an instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  inline std::size_t alignment(TypeHeader const* type) const {
    return (*this)[type].alignment();
  }

  /// Implements `alignment` for built-in types.
  constexpr std::size_t alignment(BuiltinHeader const* h) const {
    switch (h->raw_value) {
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

  /// Returns the alignment of `field`.
  ///
  /// - Requires: the type of `field` has been declared and defined in `this`.
  inline std::size_t alignment(Field const& field) const {
    return field.out_of_line() ? alignof(void*) : alignment(field.type());
  }

  /// Returns the number of bytes from the start of one instance of `type` to the start of the next
  /// when stored in contiguous memory.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  inline std::size_t stride(TypeHeader const* type) const {
    auto x = round_up_to_nearest_multiple(size(type), alignment(type));
    return x < 1 ? 1 : x;
  }

  /// Returns the offset of the `i`-th field of `m`.
  ///
  /// - Note: An instance of a sum type with more than two cases has only one field, representing
  ///   its tag. The payload is stored at the base address and is not counted as a field. Sum types
  ///   with less than two cases have no tag and thus no field.
  ///
  /// - Requires: `m` is the metatype of a product or sum type that has been declared and defined
  ///   in `this`, and `i` is less the number of fields in `m`.
  inline std::size_t offset(Metatype const& m, std::size_t i) const {
    return m.offsets()[i];
  }

  /// Returns the offset of the `i`-th field of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `i` is less the number of
  ///   fields in an instance of `type`.
  inline std::size_t offset(TypeHeader const* type, std::size_t i) const {
    auto const& m = (*this)[type];
    return offset(m, i);
  }

  /// Returns `base` advanced by the offset of the `i`-th field of `m`.
  ///
  /// New storage is allocated iff the field whose address is computed is stored out-of-line and
  /// not already allocated. The returned address points at memory capable of storing an instance
  /// of the field's type.
  ///
  /// - Requires: `m` is the metatype of a product or sum type that has been declared and defined
  ///   in `this`, and `i` is less the number of fields in `m`.
  void* address_of(Metatype const& m, std::size_t i, void* base) const;

  /// Returns `base` advanced by the offset of the `i`-th field of `type`.
  ///
  /// New storage is allocated iff the field whose address is computed is stored out-of-line and
  /// not already allocated. The returned address points at memory capable of storing an instance
  /// of the field's type.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `i` is less the number of
  ///   fields in an instance of `type`.
  inline void* address_of(TypeHeader const* type, std::size_t i, void* base) const {
    return address_of((*this)[type], i, base);
  }

  /// Calls `action` with the base address of a buffer with enough capacity to store `count`
  /// instances of `type`.
  ///
  /// `action` is called with a pointer to a zero-initialized buffer properly aligned and large
  /// enough to hold `count` instances of `type` contiguously (use `stride` to compute the offset
  /// of each position). The buffer is automatically deallocated after `action` returns, at which
  /// point the pointer passed to the lambda is invalid. Any instance stored in the temporary
  /// buffer must be be deinitialized before `action` returns.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  template<typename A>
  void with_temporary_allocation(TypeHeader const* type, std::size_t count, A action) const {
    auto s = (count == 1) ? size(type) : stride(type) * count;
    auto a = alignment(type);
    auto x = a - 1;

    if (s == 0) { return action(nullptr); }

    std::byte buffer[s + x];
    auto b = reinterpret_cast<uintptr_t>(&buffer);
    b += b & x;

    auto base_address = reinterpret_cast<void*>(b);
    std::memset(base_address, 0, s);
    action(base_address);
  }

  /// Initializes `target` with a copy of `source`, which is an instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  template<typename T>
  inline void copy_initialize_builtin(BuiltinHeader const* type, void* target, T source) const {
    if (size(type) != sizeof(T)) { throw std::invalid_argument("bad source"); }
    copy_initialize(type, target, &source);
  }

  /// Initializes `target`, which points to storage for an instance of `type`, to a copy of the
  /// value stored at `source`, which is an instance of the `tag`-th case of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void copy_initialize_enum(
    EnumHeader const* type, std::size_t tag, void* target, void* source
  ) const;

  /// Initializes `target` with a copy of the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  inline void copy_initialize(TypeHeader const* type, void* target, void* source) const {
    type->copy_initialize(target, source, *this);
  }

  /// Implements `copy_initialize` for built-in types.
  inline void copy_initialize(BuiltinHeader const* h, void* target, void* source) const {
    memcpy(target, source, size(h));
  }

  /// Implements `copy_initialize` for lambda types.
  void copy_initialize(LambdaHeader const* h, void* target, void* source) const;

  /// Implements `copy_initialize` for struct types.
  void copy_initialize(StructHeader const* h, void* target, void* source) const;

  /// Implements `copy_initialize` for enum types.
  void copy_initialize(EnumHeader const* h, void* target, void* source) const;

  /// Destroys the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  inline void deinitialize(TypeHeader const* type, void* source) const {
    type->deinitialize(source, *this);
  }

  /// Implements `deinitialize` for built-in types.
  inline void deinitialize(BuiltinHeader const* h, void* source) const {}

  /// Implements `deinitialize` for lambda types.
  void deinitialize(LambdaHeader const* h, void* source) const;

  /// Implements `deinitialize` for struct types.
  void deinitialize(StructHeader const* h, void* source) const;

  /// Implements `deinitialize` for enum types.
  void deinitialize(EnumHeader const* h, void* source) const;

  /// Deinitializes the value of `field`, which is stored at `source`.
  ///
  /// - Requires: the type of `field` has been declared and defined in `this`.
  void deinitialize(Field const& field, void* source) const;

  /// Writes to `stream` a textual representation of the value stored at `source`, which is an
  /// instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `source` is initialzed.
  inline void dump_instance(std::ostream& stream, TypeHeader const* type, void* source) const {
    type->dump_instance(stream, source, *this);
  }

  /// Implements `dump_instance` for built-in types.
  void dump_instance(std::ostream& stream, BuiltinHeader const* type, void* source) const;

  /// Implements `dump_instance` for lambda types.
  void dump_instance(std::ostream& stream, LambdaHeader const* type, void* source) const;

  /// Implements `dump_instance` for struct types.
  void dump_instance(std::ostream& stream, StructHeader const* type, void* source) const;

  /// Implements `dump_instance` for enum types.
  void dump_instance(std::ostream& stream, EnumHeader const* type, void* source) const;

  /// Returns a description of the value stored at `source`, which is an instance of `type`.
  inline std::string describe_instance(TypeHeader const* type, void* source) const {
    std::stringstream o;
    dump_instance(o, type, source);
    return o.str();
  }

};

template<>
struct MetatypeConstructor<BuiltinHeader> {
  Metatype operator()(BuiltinHeader const* h, TypeStore& store) {
    auto s = store.size(h);
    auto a = store.alignment(h);
    return Metatype{s, a, true, {}, {}};
  }
};

}
