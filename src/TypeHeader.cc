#include "TypeHeader.h"

namespace xst {

TypeHeader::~TypeHeader() {
  switch (tag()) {
    case xst::none_tag:
      return;
    case xst::builtin_tag:
      return;
    case xst::enum_tag:
      this->as<xst::EnumHeader>()->~EnumHeader();
    case xst::struct_tag:
      this->as<xst::StructHeader>()->~StructHeader();
  }
}

std::size_t TypeHeader::hash_value() const {
  switch (tag()) {
    case xst::none_tag:
      return 0;
    case xst::builtin_tag:
      return this->as<BuiltinHeader>()->hash_value();
    case xst::enum_tag:
      return this->as<xst::EnumHeader>()->hash_value();
    case xst::struct_tag:
      return this->as<xst::StructHeader>()->hash_value();
  }
}

bool TypeHeader::equal_to(TypeHeader const& other) const {
  // Fast path: both headers have the same raw representation.
  if (this->raw_value == other.raw_value) { return true; }

  // Are the two headers of the same type?
  auto t = tag();
  if (t != other.tag()) { return false; }

  // Dispatch to the correct equality operator.
  switch (tag()) {
    case xst::none_tag:
      return false;
    case xst::builtin_tag:
      return *(this->as<BuiltinHeader>()) == *(other.as<BuiltinHeader>());
    case xst::enum_tag:
      return *(this->as<EnumHeader>()) == *(other.as<EnumHeader>());
    case xst::struct_tag:
      return *(this->as<StructHeader>()) == *(other.as<StructHeader>());
  }
  return false;
}

std::string TypeHeader::description() const {
  std::stringstream s;
  switch (tag()) {
    case xst::none_tag:
      s << "nil"; break;
    case xst::builtin_tag:
      s << this->as<xst::BuiltinHeader>()->description(); break;
    case xst::enum_tag:
      s << this->as<xst::EnumHeader>()->description(); break;
    case xst::struct_tag:
      s << this->as<xst::StructHeader>()->description(); break;
  }
  return s.str();
}

}
