#include "TypeStore.h"

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

Metatype const& TypeStore::define(StructHeader const* t, std::vector<Field>&& fields) {
  auto& m = initialize_metatype(t->widened());
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
  auto& m = initialize_metatype(t->widened());
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

bool TypeStore::is_trivial(TypeHeader const* h) const {
  switch (h->tag()) {
    case none_tag:
      return false;

    case builtin_tag:
      return true;

    case enum_tag:
    case struct_tag:
      auto const& m = (*this)[h];
      precondition(m.defined(), h->description() + " is not defined");
      return is_trivial(m);
  }
}

bool TypeStore::is_trivial(Metatype const& m) const {
  return std::all_of(m.fields.begin(), m.fields.end(), [&](auto const& f) {
    return !f.out_of_line() && is_trivial(f.type());
  });
}

std::size_t TypeStore::size(TypeHeader const* h) const {
  switch (h->tag()) {
    case none_tag:
      return 0;
    case builtin_tag:
      return size(h->as<BuiltinHeader>());
    case enum_tag:
      return size(h->as<EnumHeader>());
    case struct_tag:
      return size(h->as<StructHeader>());
  }
}

std::size_t TypeStore::size(BuiltinHeader const* h) const {
  switch (h->tag()) {
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

std::size_t TypeStore::alignment(TypeHeader const* h) const {
  switch (h->tag()) {
    case none_tag:
      return 0;
    case builtin_tag:
      return alignment(h->as<BuiltinHeader>());
    case enum_tag:
      return alignment(h->as<EnumHeader>());
    case struct_tag:
      return alignment(h->as<StructHeader>());
  }
}

std::size_t TypeStore::alignment(BuiltinHeader const* h) const {
  switch (h->tag()) {
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

void TypeStore::copy_initialize_enum(
  EnumHeader const* h, std::size_t tag, void* target, void* source
) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  // Copy the payload.
  auto t0 = address_of(m, 0, target);
  copy_initialize(m.fields.at(tag).type(), t0, source);

  // Set the tag.
  auto t1 = static_cast<uint16_t*>(address_of(m, 1, target));
  *t1 = static_cast<uint16_t>(tag);
}

void TypeStore::copy_initialize(TypeHeader const* h, void* target, void* source) const {
  switch (h->tag()) {
    case none_tag:
      return;
    case builtin_tag:
      memcpy(target, source, size(h));
    case enum_tag:
      return copy_initialize(h->as<EnumHeader>(), target, source);
    case struct_tag:
      return copy_initialize(h->as<StructHeader>(), target, source);
  }
}

void TypeStore::copy_initialize(EnumHeader const* h, void* target, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  if (is_trivial(m)) {
    memcpy(target, source, size(h->widened()));
  } else {
    auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
    auto f = m.fields.at(*tag);

    // Copy the payload.
    auto t0 = address_of(m, 0, target);
    auto s0 = address_of(m, 0, source);
    copy_initialize(f.type(), t0, s0);

    // Copy the tag.
    auto t1 = static_cast<uint16_t*>(address_of(m, 1, target));
    *t1 = static_cast<uint16_t>(*tag);
  }
}

void TypeStore::copy_initialize(StructHeader const* h, void* target, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  if (is_trivial(m)) {
    memcpy(target, source, size(h->widened()));
  } else {
    for (auto i = 0; i < m.fields.size(); ++i) {
      auto t = address_of(m, i, target);
      auto s = address_of(m, i, source);
      copy_initialize(m.fields[i].type(), t, s);
    }
  }
}

inline void TypeStore::deinitialize(TypeHeader const* h, void* source) const {
  switch (h->tag()) {
    case none_tag:
      return;
    case builtin_tag:
      return;
    case enum_tag:
      return deinitialize(h->as<EnumHeader>(), source);
    case struct_tag:
      return deinitialize(h->as<StructHeader>(), source);
  }
}

void TypeStore::deinitialize(EnumHeader const* h, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
  auto s = address_of(m, 0, source);
  auto f = m.fields.at(*tag);
  deinitialize(f, s);
};

void TypeStore::deinitialize(StructHeader const* h, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  for (auto i = 0; i < m.fields.size(); ++i) {
    auto s = address_of(m, i, source);
    auto f = m.fields[i];
    deinitialize(f, s);
  }
};

void TypeStore::deinitialize(Field const& f, void* s) const {
  deinitialize(f.type(), s);
  if (f.out_of_line()) {
    aligned_free(s);
  }
}

void TypeStore::dump_instance(std::ostream& s, TypeHeader const* h, void* source) const {
  switch (h->tag()) {
    case none_tag:
      s << "nil"; break;
    case builtin_tag:
      dump_instance(s, h->as<BuiltinHeader>(), source); break;
    case enum_tag:
      dump_instance(s, h->as<EnumHeader>(), source); break;
    case struct_tag:
      dump_instance(s, h->as<StructHeader>(), source); break;
  }
}

void TypeStore::dump_instance(std::ostream& s, BuiltinHeader const* h, void* source) const {
  switch (h->tag()) {
    case BuiltinHeader::boolean:
      s << (*static_cast<bool*>(source) ? "true" : "false"); break;
    case BuiltinHeader::i32:
      s << *static_cast<int32_t*>(source); break;
    case BuiltinHeader::i64:
      s << *static_cast<int64_t*>(source); break;
    case BuiltinHeader::str:
      s << *static_cast<char const**>(source); break;
  }
}

void TypeStore::dump_instance(std::ostream& s, EnumHeader const* h, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
  auto p = address_of(m, 0, source);
  auto f = m.fields.at(*tag);

  s << h->description() << "(";
  dump_instance(s, f.type(), p);
  s << ")";
}

void TypeStore::dump_instance(std::ostream& s, StructHeader const* h, void* source) const {
  auto const& m = (*this)[h->widened()];
  precondition(m.defined(), h->description() + " is not defined");

  s << h->description() << "(";
  for (auto i = 0; i < m.fields.size(); ++i) {
    if (i > 0) { s << ", "; }
    auto p = address_of(m, i, source);
    dump_instance(s, m.fields[i].type(), p);
  }
  s << ")";
}

}
