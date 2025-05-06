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
std::vector<std::size_t> offsets(
  std::vector<Field> const& fields, TypeStore const& store
) {
  std::vector<std::size_t> result{0};
  for (auto i = 1; i < fields.size(); ++i) {
    auto const& f = fields[i];
    auto p = result.at(i - 1) + store.size(fields[i - 1]);
    auto q = round_up_to_nearest_multiple(p, store.alignment(f));
    result.push_back(static_cast<uint32_t>(q));
  }
  return result;
}

Metatype::Metatype(
  std::size_t size, std::size_t alignment, bool trivial,
  std::vector<Field>&& fields,
  std::vector<std::size_t>&& offsets
) {
  auto field_count = fields.size();
  precondition(field_count == offsets.size(), "inconsistent fields and offsets");

  // Can we store everything inline?
  if ((field_count == 0) && !((size | alignment) & ~0xffff) && (sizeof(std::uintptr_t) >= 8)) {
    data = (size << 32) | (alignment << 16) | (trivial ? 0b11 : 0b01);
  }

  // Use out-of-line storage.
  else {
    auto buffer = new std::size_t[3 + field_count + field_count];
    buffer[0] = size;
    buffer[1] = alignment;
    buffer[2] = field_count;

    auto f = fields.begin();
    auto o = offsets.begin();
    for (auto i = 0; i < field_count; ++i) {
      buffer[i + 3] = static_cast<std::size_t>((f++)->raw_value);
      buffer[i + 3 + field_count] = static_cast<std::size_t>(*(o++));
    }

    data = reinterpret_cast<uintptr_t>(buffer) | (trivial ? 0b10 : 0b00);
  }
}

Metatype& TypeStore::get_undefined_metatype(TypeHeader const* t) {
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
  auto& m = get_undefined_metatype(t);

  if (fields.empty()) {
    m = Metatype{0, 1, true, {}, {}};
  } else {
    // Compute field offsets.
    auto offsets = xst::offsets(fields, *this);

    // Compute size and alignment.
    std::size_t a = 1;
    for (auto const& f : fields) { a = std::max(a, alignment(f)); }
    std::size_t s = size(fields.back()) + offsets.back();

    // Define the metatype.
    auto t = all_trivial(fields);
    m = Metatype{s, a, t, std::move(fields), std::move(offsets)};
  }

  return m;
}

Metatype const& TypeStore::define(EnumHeader const* t, std::vector<Field>&& fields) {
  auto& m = get_undefined_metatype(t);

  if (fields.empty()) {
    m = Metatype{0, 1, true, {}, {}};
  } else if (fields.size() == 1) {
    auto s = size(fields[0]);
    auto a = alignment(fields[0]);
    auto t = is_trivial(fields[0]);
    m = Metatype{s, a, t, std::move(fields), {0}};
  } else {
    // Compute size and alignment.
    std::size_t s = 0;
    std::size_t a = 1;
    for (auto const& f : fields) {
      s = std::max(s, size(f));
      a = std::max(a, alignment(f));
    }
    auto tag_offset = round_up_to_nearest_multiple(s, alignof(uint16_t));
    s = tag_offset + sizeof(uint16_t);
    a = std::max(a, alignof(uint16_t));

    // Define the metatype.
    auto t = all_trivial(fields);
    m = Metatype{s, a, t, std::move(fields), {0, tag_offset}};
  }

  return m;
}

Metatype const& TypeStore::operator[](TypeHeader const* t) const {
  auto entry = metatype.find(DereferencingKey<TypeHeader>{t});
  if (entry != metatype.end()) {
    precondition(entry->second.defined(), t->description() + " is not defined");
    return entry->second;
  } else {
    throw std::out_of_range(t->description() + " is unknown");
  }
}

void* TypeStore::address_of(Metatype const& m, std::size_t i, void* base) const {
  auto& field = m.fields()[i];
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

void BuiltinHeader::copy_initialize(void* target, void* source, TypeStore const& s) const {
  s.copy_initialize(this, target, source);
}

void TypeStore::copy_initialize(LambdaHeader const* h, void* target, void* source) const {
  precondition(false, "TODO");
}

void LambdaHeader::copy_initialize(void* target, void* source, TypeStore const& s) const {
  s.copy_initialize(this, target, source);
}

void TypeStore::copy_initialize(StructHeader const* h, void* target, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  if (m.is_trivial()) {
    memcpy(target, source, size(h));
  } else {
    auto fields = m.fields();
    for (auto i = 0; i < fields.size(); ++i) {
      auto t = address_of(m, i, target);
      auto s = address_of(m, i, source);
      copy_initialize(fields[i].type(), t, s);
    }
  }
}

void StructHeader::copy_initialize(void* target, void* source, TypeStore const& s) const {
  s.copy_initialize(this, target, source);
}

void TypeStore::copy_initialize(EnumHeader const* h, void* target, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  if (m.is_trivial()) {
    memcpy(target, source, size(h));
  } else {
    auto fields = m.fields();
    auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
    auto f = fields[*tag];

    // Copy the payload.
    auto t0 = address_of(m, 0, target);
    auto s0 = address_of(m, 0, source);
    copy_initialize(f.type(), t0, s0);

    // Copy the tag, unless there's only one field.
    if (fields.size() > 1) {
      *static_cast<uint16_t*>(address_of(m, 1, target)) = static_cast<uint16_t>(*tag);
    }
  }
}

void EnumHeader::copy_initialize(void* target, void* source, TypeStore const& s) const {
  s.copy_initialize(this, target, source);
}

void TypeStore::copy_initialize_enum(
  EnumHeader const* type, std::size_t tag, void* target, void* source
) const {
  auto const& m = (*this)[type];
  precondition(m.defined(), type->description() + " is not defined");

  // Copy the payload.
  auto t0 = address_of(m, 0, target);
  copy_initialize(m.fields()[tag].type(), t0, source);

  // Set the tag.
  auto t1 = static_cast<uint16_t*>(address_of(m, 1, target));
  *t1 = static_cast<uint16_t>(tag);
}

void BuiltinHeader::deinitialize(void* source, TypeStore const& s) const {
  s.deinitialize(this, source);
}

void TypeStore::deinitialize(LambdaHeader const* h, void* source) const {
  precondition(false, "TODO");
};

void LambdaHeader::deinitialize(void* source, TypeStore const& s) const {
  s.deinitialize(this, source);
};

void TypeStore::deinitialize(StructHeader const* h, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  if (m.is_trivial()) { return; }

  auto fields = m.fields();
  for (auto i = 0; i < fields.size(); ++i) {
    auto s = address_of(m, i, source);
    auto f = fields[i];
    deinitialize(f, s);
  }
};

void StructHeader::deinitialize(void* source, TypeStore const& s) const {
  s.deinitialize(this, source);
};

void TypeStore::deinitialize(EnumHeader const* h, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  if (m.is_trivial()) { return; }

  auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
  auto s = address_of(m, 0, source);
  auto f = m.fields()[*tag];
  deinitialize(f, s);
}

void EnumHeader::deinitialize(void* source, TypeStore const& s) const {
  s.deinitialize(this, source);
};

void TypeStore::deinitialize(Field const& f, void* s) const {
  deinitialize(f.type(), s);
  if (f.out_of_line()) {
    aligned_free(s);
  }
}

void TypeStore::dump_instance(std::ostream& o, BuiltinHeader const* h, void* source) const {
  switch (h->raw_value) {
    case BuiltinHeader::boolean:
      o << (*static_cast<bool*>(source) ? "true" : "false"); break;
    case BuiltinHeader::i32:
      o << *static_cast<int32_t*>(source); break;
    case BuiltinHeader::i64:
      o << *static_cast<int64_t*>(source); break;
    case BuiltinHeader::str:
      o << *static_cast<char const**>(source); break;
  }
}

void BuiltinHeader::dump_instance(std::ostream& o, void* source, TypeStore const& s) const {
  s.dump_instance(o, this, source);
}

void TypeStore::dump_instance(std::ostream& o, LambdaHeader const* h, void* source) const {
  precondition(false, "TODO");
}

void LambdaHeader::dump_instance(std::ostream& o, void* source, TypeStore const& s) const {
  s.dump_instance(o, this, source);
}

void TypeStore::dump_instance(std::ostream& o, StructHeader const* h, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  o << h->description() << "(";
  auto fields = m.fields();
  for (auto i = 0; i < fields.size(); ++i) {
    if (i > 0) { o << ", "; }
    auto s = address_of(m, i, source);
    dump_instance(o, fields[i].type(), s);
  }
  o << ")";
}

void StructHeader::dump_instance(std::ostream& o, void* source, TypeStore const& s) const {
  s.dump_instance(o, this, source);
}

void TypeStore::dump_instance(std::ostream& o, EnumHeader const* h, void* source) const {
  auto const& m = (*this)[h];
  precondition(m.defined(), h->description() + " is not defined");

  auto tag = static_cast<uint16_t*>(address_of(m, 1, source));
  auto s = address_of(m, 0, source);
  auto f = m.fields()[*tag];

  o << h->description() << "(";
  dump_instance(o, f.type(), s);
  o << ")";
}

void EnumHeader::dump_instance(std::ostream& o, void* source, TypeStore const& s) const {
  s.dump_instance(o, this, source);
}

}
