#include "TypeHeader.h"
#include "TypeStore.h"

#include <cassert>

namespace xst {

/// Ensures that the given condition is satisfied or throws an error.
void precondition(bool test, std::string const& error) {
  if (!test) { throw std::logic_error(error); }
}

/// Allocates `s` bytes of storage aligned `a`, initialized to 0 iff `zero_initialize` is set.
void* aligned_alloc(std::size_t a, std::size_t s, bool zero_initialize = false) {
  if (s == 0) { return nullptr; }

  using Head = std::size_t;

  auto x = std::max(a, alignof(Head)) - 1;
  auto b = new std::byte[sizeof(Head) + s + x];

  Head offset = round_up_to_nearest_multiple(sizeof(Head), a);
  auto payload = b + offset;
  auto head = payload - sizeof(Head);
  memcpy(head, &offset, sizeof(Head));

  if (zero_initialize) {
    std::memset(payload, 0, s);
  }

  return payload;
}

/// Deallocates memory allocated by `aligned_alloc`.
///
/// This function is a no-op if `p` is `nullptr`.
void aligned_free(void* p) {
  if (p == nullptr) { return; }

  using Head = std::size_t;

  Head offset;
  auto payload = static_cast<std::byte*>(p);
  auto head = payload - sizeof(Head);
  memcpy(&offset, head, sizeof(Head));
  auto b = payload - offset;
  delete[] b;
}

/// Returns the number of bytes before the given fields, reading metatypes from `store`.
std::vector<uint32_t> offsets(
  std::vector<Field> const& fields, TypeStore const& store
) {
  std::vector<std::uint32_t> result{0};
  for (auto i = 1; i < fields.size(); ++i) {
    auto const& f = fields[i];
    auto p = result.at(i - 1) + store.size(fields[i - 1]);
    auto q = round_up_to_nearest_multiple(p, store.alignment(f));
    result.push_back(static_cast<uint32_t>(q));
  }
  return result;
}

Metatype& TypeStore::initialize_metatype(TypeHeader const* t) {
  auto entry = metatype.find(DereferencingKey<TypeHeader>{t});
  if (entry == metatype.end()) {
    throw std::out_of_range(t->description() + " is unknown");
  } else if (entry->second.defined()) {
    throw std::logic_error(t->description() + " is already defined");
  } else {
    return entry->second;
  }
}

bool TypeStore::defined(TypeHeader const* t) const {
  auto entry = metatype.find(DereferencingKey<TypeHeader>{t});
  return (entry != metatype.end()) && entry->second.defined();
}

Metatype const& TypeStore::define(StructHeader const* t, std::vector<Field>&& fields) {
  auto& m = initialize_metatype(t);
  auto begin = static_cast<uint32_t>(cache.size());

  // Define the fields.
  m.fields = std::move(fields);

  if (m.fields.empty()) {
    cache.push_back(0);  // size
    cache.push_back(1);  // alignment
  } else {
    // Compute field offsets.
    auto offsets = xst::offsets(m.fields, *this);

    // Compute alignment.
    std::size_t a = 1;
    for (auto const& f : m.fields) { a = std::max(a, alignment(f)); }

    // Fill the cache.
    cache.push_back(static_cast<uint32_t>(size(m.fields.back())) + offsets.back());
    cache.push_back(static_cast<uint32_t>(a));
    std::copy(offsets.begin() + 1, offsets.end(), std::back_inserter(cache));
  }

  m.cache_begin = begin;
  return m;
}

Metatype const& TypeStore::define(EnumHeader const* t, std::vector<Field>&& fields) {
  auto& m = initialize_metatype(t);
  auto begin = static_cast<uint32_t>(cache.size());

  // Define the fields.
  m.fields = std::move(fields);

  if (m.fields.empty()) {
    cache.push_back(0);  // size
    cache.push_back(1);  // alignment
  } if (fields.size() == 1) {
    cache.push_back(static_cast<uint32_t>(size(m.fields[0])));
    cache.push_back(static_cast<uint32_t>(alignment(m.fields[0])));
  } else {
    // Compute size and alignment.
    std::size_t s = 0;
    std::size_t a = 1;
    for (auto const& f : m.fields) {
      s = std::max(s, size(f));
      a = std::max(a, alignment(f));
    }
    auto tag_offset = round_up_to_nearest_multiple(s, alignof(uint16_t));
    s = tag_offset + sizeof(uint16_t);
    a = std::max(a, alignof(uint16_t));

    // Fill the cache.
    cache.push_back(static_cast<uint32_t>(s));
    cache.push_back(static_cast<uint32_t>(a));
    cache.push_back(static_cast<uint32_t>(tag_offset));
  }

  m.cache_begin = begin;
  return m;
}

Metatype const& TypeStore::operator[](TypeHeader const* t) const {
  auto entry = metatype.find(DereferencingKey<TypeHeader>{t});
  if (entry != metatype.end()) {
    return entry->second;
  } else {
    throw std::out_of_range(t->description() + " is unknown");
  }
}

void* TypeStore::address_of(Metatype const& m, std::size_t i, void* base) const {
  auto& field = m.fields.at(i);
  auto field_address = static_cast<void*>(static_cast<char*>(base) + offset(m, i));

  if (field.out_of_line()) {
    auto p = static_cast<void**>(field_address);

    // Should the target be allocated?
    if (*p == nullptr) {
      auto t = field.type();
      auto s = size(t);
      auto a = alignment(t);
      *p = aligned_alloc(a, s, true);
    }

    return *p;
  } else {
    return field_address;
  }
}

bool TypeStore::is_trivial(Metatype const& m) const {
  return std::all_of(m.fields.begin(), m.fields.end(), [&](auto const& f) {
    return !f.out_of_line() && is_trivial(f.type());
  });
}

bool LambdaHeader::is_trivial(TypeStore const&) const {
  return false;
}

bool CompositeHeader::is_trivial(TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");
  return store.is_trivial(m);
}

std::size_t BuiltinHeader::size(TypeStore const&) const {
  switch (raw_value) {
    case Value::boolean: return alignof(bool);
    case Value::i32: return alignof(int32_t);
    case Value::i64: return alignof(int64_t);
    case Value::str: return alignof(char const*);
  }
}

void BuiltinHeader::copy_initialize(void* target, void* source, TypeStore const& store) const {
  memcpy(target, source, size(store));
}

void BuiltinHeader::deinitialize(void* source, TypeStore const&) const {};

std::size_t BuiltinHeader::alignment(TypeStore const&) const {
  switch (raw_value) {
    case Value::boolean: return alignof(bool);
    case Value::i32: return alignof(int32_t);
    case Value::i64: return alignof(int64_t);
    case Value::str: return alignof(char const*);
  }
}

std::size_t LambdaHeader::size(TypeStore const& store) const {
  return sizeof(void*) + sizeof(void*);
}

std::size_t LambdaHeader::alignment(TypeStore const& store) const {
  return alignof(void*);
}

void LambdaHeader::copy_initialize(void* target, void* source, TypeStore const& store) const {
  precondition(false, "TODO");
}

void LambdaHeader::deinitialize(void* source, TypeStore const&) const {
  precondition(false, "TODO");
};

std::size_t CompositeHeader::size(TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");
  return store.cache.at(m.cache_begin);
}

std::size_t CompositeHeader::alignment(TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");
  return store.cache.at(m.cache_begin + 1);
}

void StructHeader::copy_initialize(void* target, void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");

  if (store.is_trivial(m)) {
    memcpy(target, source, size(store));
  } else {
    for (auto i = 0; i < m.fields.size(); ++i) {
      auto t = store.address_of(m, i, target);
      auto s = store.address_of(m, i, source);
      store.copy_initialize(m.fields[i].type(), t, s);
    }
  }
}

void StructHeader::deinitialize(void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");

  for (auto i = 0; i < m.fields.size(); ++i) {
    auto s = store.address_of(m, i, source);
    auto f = m.fields[i];
    store.deinitialize(f, s);
  }
};

void TypeStore::copy_initialize_enum(
  EnumHeader const* type, std::size_t tag, void* target, void* source
) const {
  auto const& m = (*this)[type];
  precondition(m.defined(), type->description() + " is not defined");

  // Copy the payload.
  auto t0 = address_of(m, 0, target);
  copy_initialize(m.fields.at(tag).type(), t0, source);

  // Set the tag.
  auto t1 = static_cast<uint16_t*>(address_of(m, 1, target));
  *t1 = static_cast<uint16_t>(tag);
}

void EnumHeader::copy_initialize(void* target, void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");

  if (store.is_trivial(m)) {
    memcpy(target, source, size(store));
  } else {
    auto tag = static_cast<uint16_t*>(store.address_of(m, 1, source));
    auto f = m.fields.at(*tag);

    // Copy the payload.
    auto t0 = store.address_of(m, 0, target);
    auto s0 = store.address_of(m, 0, source);
    store.copy_initialize(f.type(), t0, s0);

    // Copy the tag.
    auto t1 = static_cast<uint16_t*>(store.address_of(m, 1, target));
    *t1 = static_cast<uint16_t>(*tag);
  }
}

void EnumHeader::deinitialize(void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");

  auto tag = static_cast<uint16_t*>(store.address_of(m, 1, source));
  auto s = store.address_of(m, 0, source);
  auto f = m.fields.at(*tag);
  store.deinitialize(f, s);
};

void TypeStore::deinitialize(Field const& f, void* s) const {
  deinitialize(f.type(), s);
  if (f.out_of_line()) {
    aligned_free(s);
  }
}

void BuiltinHeader::dump_instance(std::ostream& o, void* s, TypeStore const&) const {
  switch (raw_value) {
    case Value::boolean:
      o << (*static_cast<bool*>(s) ? "true" : "false"); break;
    case Value::i32:
      o << *static_cast<int32_t*>(s); break;
    case Value::i64:
      o << *static_cast<int64_t*>(s); break;
    case Value::str:
      o << *static_cast<char const**>(s); break;
  }
}

void LambdaHeader::dump_instance(std::ostream& o, void* s, TypeStore const&) const {
  precondition(false, "TODO");
}

void StructHeader::dump_instance(std::ostream& o, void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");

  o << description() << "(";
  for (auto i = 0; i < m.fields.size(); ++i) {
    if (i > 0) { o << ", "; }
    auto s = store.address_of(m, i, source);
    store.dump_instance(o, m.fields[i].type(), s);
  }
  o << ")";
}

void EnumHeader::dump_instance(std::ostream& o, void* source, TypeStore const& store) const {
  auto const& m = store[this];
  precondition(m.defined(), description() + " is not defined");


  auto tag = static_cast<uint16_t*>(store.address_of(m, 1, source));
  auto s = store.address_of(m, 0, source);
  auto f = m.fields.at(*tag);

  o << description() << "(";
  store.dump_instance(o, f.type(), s);
  o << ")";
}

}
