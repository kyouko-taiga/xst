#pragma once

// #include "TypeHeader.h"
#include "TypeHeader.h"
#include "Utilities.h"

#include <unordered_map>

namespace xst {

/// A type identifier and a flag that is set if its instance is stored indirectly.
struct Field {
private:

  /// A pointer to a type header and a bit represented by the least significant bit.
  uintptr_t raw_value;

public:

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

  /// The stored properties of the type.
  std::vector<Field> fields;

  /// The offset of the cache storing the computed properties of this instance.
  ///
  /// The metatype is considered undefined if all the bits of the property are set.
  uint32_t cache_begin = std::numeric_limits<uint32_t>::max();

  /// Returns `true` iff `this` has been defined.
  inline bool defined() const {
    return cache_begin != std::numeric_limits<uint32_t>::max();
  }

};

struct TypeStore {
private:

  /// An array containing the type ifentifiers allocated in this store.
  std::vector<std::unique_ptr<TypeHeader>> identifiers;

  /// A table from a type identifier to its corresponding metatype.
  std::unordered_map<DereferencingKey<TypeHeader>, Metatype> metatype;

  /// An array containing the size, alignment, and field offsets of struct metatypes.
  ///
  /// This array caches the size, alignment, and field offsets of composite types that have been
  /// defined in this store. Each set of property is stored contiguously from an offset that is
  /// stored in the corresponding metatype once it has been defined.
  ///
  /// The offset of the first field, which is always 0, is not stored cached. For instance, if the
  /// metatype of a struct is defined as a pair `{i64, i32}`, the cache will contain a subsequence
  /// `[12, 8, 8]` assuming `i64` is aligned at 8. The first element is the size of an instance,
  /// the second is its alignment, the last is the offset of the second component of the pair.
  ///
  /// The computed properties of built-in and function types are not cached.
  mutable std::vector<uint32_t> cache;

  /// Initializes the definition of `t`'s metatype.
  ///
  /// - Requires: `t` has been declared and never explicitly defined in `this`.
  Metatype& initialize_metatype(TypeHeader const* t);

public:

  /// Creates an empty instance.
  TypeStore() = default;

  /// Creates an instance in which `types` are declared and defined with `define`.
  template<typename F>
  TypeStore(std::initializer_list<TypeHeader const*> types, F define) {
    for (auto const& t : types) {
      metatype.insert_or_assign(DereferencingKey{t}, Metatype{});
    }
    define(*this);
  }

  /// Returns a pointer to the unique instance equal to `identifier` in this store.
  template<typename T>
  T const* declare(T&& identifier) {
    auto entry = metatype.find(DereferencingKey<TypeHeader>{identifier.widened()});

    // The identifier is already known.
    if (entry != metatype.end()) {
      return entry->first.value->as<T>();
    }

    // The identifier is unknown; intern it.
    else {
      identifiers.emplace_back(std::make_unique<TypeHeader>(std::move(identifier)));
      auto const* p = identifiers.back().get();
      metatype.emplace(std::make_pair(DereferencingKey{p}, Metatype{}));
      return p->as<T>();
    }
  }

  /// Returns a pointer to the unique instance identifying `tag` in this store.
  inline BuiltinHeader const* declare(BuiltinHeader::Tag tag) {
    return declare(BuiltinHeader{tag});
  }

  /// Returns a pointer to the unique instance identifying `tag`, declaring it if necessary.
  inline BuiltinHeader const* get(BuiltinHeader::Tag tag) {
    return declare(BuiltinHeader{tag});
  }

  /// Returns `true` iff `type` has been declared and defined in `this`.
  inline bool defined(TypeHeader const* t) const {
    auto entry = metatype.find(DereferencingKey<TypeHeader>{t});
    return (entry != metatype.end()) && entry->second.defined();
  }

  /// Returns `true` iff `type` has been declared and defined in `this`.
  template<TypeHeader::Tag tag>
  inline bool defined(CompositeHeader<tag> const* t) const {
    auto entry = metatype.find(DereferencingKey<TypeHeader>{t->widened()});
    return (entry != metatype.end()) && entry->second.defined();
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

  /// Returns `true` iff instances of `type` do not involve out-of-line storage.
  ///
  /// - Requires `type` has been declared and defined in `this`.
  bool is_trivial(TypeHeader const* type) const;

  /// Returns `true` iff none of the fields in `m` involves out-of-line storage.
  bool is_trivial(Metatype const& m) const;

  /// Returns the size of an instance of `t`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  std::size_t size(TypeHeader const* type) const;

  /// Returns the size of an instance of `t`.
  std::size_t size(BuiltinHeader const* type) const;

  /// Returns the size of an instance of `t`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  template<TypeHeader::Tag tag>
  std::size_t size(CompositeHeader<tag> const* type) const {
    auto const& m = (*this)[type->widened()];
    // precondition(m.defined(), type->description() + " is not defined");
    return cache.at(m.cache_begin);
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
  std::size_t alignment(TypeHeader const* type) const;

  /// Returns the alignment of an instance of `type`.
  std::size_t alignment(BuiltinHeader const* type) const;

  /// Returns the size of an instance of `t`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  template<TypeHeader::Tag tag>
  std::size_t alignment(CompositeHeader<tag> const* type) const {
    auto const& m = (*this)[type->widened()];
    // precondition(m.defined(), type->description() + " is not defined");
    return cache.at(m.cache_begin + 1);
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
    if ((i == 0) || m.fields.empty()) {
      return 0;
    } else {
      // The field at index 0 has no offset and isn't stored in the cache.
      return static_cast<std::size_t>(cache.at(m.cache_begin + i + 1));
    }
  }

  /// Returns the offset of the `i`-th field of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `i` is less the number of
  ///   fields in an instance of `type`.
  inline std::size_t offset(TypeHeader const* type, std::size_t i) const {
    auto m = (*this)[type];
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
    if (size(type->widened()) != sizeof(T)) { throw std::invalid_argument("bad source"); }
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
  void copy_initialize(TypeHeader const* type, void* target, void* source) const;

  /// Initializes `target` with a copy of the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void copy_initialize(EnumHeader const* type, void* target, void* source) const;

  /// Initializes `target` with a copy of the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void copy_initialize(StructHeader const* type, void* target, void* source) const;

  /// Destroys the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void deinitialize(TypeHeader const* type, void* source) const;

  /// Destroys the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void deinitialize(EnumHeader const* type, void* source) const;

  /// Destroys the instance of `type` that is stored at `source`.
  ///
  /// - Requires: `type` has been declared and defined in `this`.
  void deinitialize(StructHeader const* type, void* source) const;

  /// Deinitializes the value of `field`, which is stored at `source`.
  ///
  /// - Requires: the type of `field` has been declared and defined in `this`.
  void deinitialize(Field const& field, void* source) const;

  /// Writes to `stream` a textual representation of the value stored at `source`, which is an
  /// instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `source` is initialzed.
  void dump_instance(std::ostream& stream, TypeHeader const* type, void* source) const;

  /// Writes to `stream` a textual representation of the value stored at `source`, which is an
  /// instance of `type`.
  void dump_instance(std::ostream& stream, BuiltinHeader const* type, void* source) const;

  /// Writes to `stream` a textual representation of the value stored at `source`, which is an
  /// instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `source` is initialzed.
  void dump_instance(std::ostream& stream, EnumHeader const* type, void* source) const;

  /// Writes to `stream` a textual representation of the value stored at `source`, which is an
  /// instance of `type`.
  ///
  /// - Requires: `type` has been declared and defined in `this` and `source` is initialzed.
  void dump_instance(std::ostream& stream, StructHeader const* type, void* source) const;

  /// Returns a description of the value stored at `source`, which is an instance of `type`.
  inline std::string describe_instance(TypeHeader const* type, void* source) const {
    std::stringstream o;
    dump_instance(o, type, source);
    return o.str();
  }

};

}
