#include "Indirect.h"
#include "TypeStore.h"

#include <iostream>
#include <variant>

namespace rt {

static auto i64 = xst::BuiltinHeader{xst::BuiltinHeader::i64};

static xst::TypeStore store{
  std::initializer_list<xst::TypeHeader const*>{i64.widened()},
  [](auto& s) {}};

xst::TypeHeader const* ListCons(xst::TypeHeader const* T);

xst::TypeHeader const* ListEmpty(xst::TypeHeader const* T);

xst::EnumHeader const* List(xst::TypeHeader const* T) {
  auto _0 = store.declare(xst::EnumHeader("List", {T}));
  if (!store.defined(_0)) {
    auto _1 = ListCons(T);
    auto _2 = ListEmpty(T);
    store.define(_0, std::vector{xst::Field{_1}, xst::Field{_2}});
  }
  return _0;
}

xst::TypeHeader const* ListCons(xst::TypeHeader const* T) {
  auto _0 = store.declare(xst::StructHeader("List.Cons", {T}));
  if (!store.defined(_0)) {
    auto _1 = store.declare(xst::EnumHeader("List", {T}))->widened();
    store.define(_0, std::vector{xst::Field{T}, xst::Field{_1, true}});
  }
  return _0->widened();
}

xst::TypeHeader const* ListEmpty(xst::TypeHeader const* T) {
  auto _0 = store.declare(xst::StructHeader("List.Empty", {T}));
  if (!store.defined(_0)) {
    store.define(_0, {});
  }
  return _0->widened();
}

}

template<typename T>
void print(T const& item) {
  std::cout << item << std::endl;
}

int main(int argc, const char * argv[]) {
  auto a0 = rt::store.get(xst::BuiltinHeader::i64)->widened();
  auto a1 = rt::ListCons(a0);
  auto a2 = rt::ListEmpty(a0);
  auto a3 = rt::List(a0);
  print(a3->description());
//  std::cout << a2->description() << std::endl;
//  std::cout << "  size:      " << rt::store.size(a2) << std::endl;
//  std::cout << "  alignment: " << rt::store.alignment(a2) << std::endl;
//
//  // Allocate List.Cons<Int64> on the stack.
//  rt::store.with_temporary_allocation(a1, 1, [&](auto p0) {
//    // Get the address of the `head` field, which is at index 0.
//    auto p1 = rt::store.address_of(a1, 0, p0);
//    // Write 42 to the `head` field.
//    rt::store.copy_initialize_builtin<uint64_t>(a0, p1, 42);
//    // Allocate List.Empty<Int64> on the stack.
//    rt::store.with_temporary_allocation(a2, 1, [&](auto p2) {
//      // Get the address of the `tail` field, which is at index 1.
//      auto p3 = rt::store.address_of(a1, 1, p0);
//      // Store a `List.Empty<Int64>` to the `tail` field, which has tag 1.
//      rt::store.copy_initialize_enum(a3, 1, p3, p2);
//      // Deinitializes the `List.Empty<Int64>` stored in `p2`.
//      rt::store.deinitialize(a2, p2);
//    });
//
//    // From this point, `p0` contains a fully initialized `List.Cons<Int64>`.
//    std::cout << rt::store.describe_instance(a1, p0) << std::endl;
//    rt::store.with_temporary_allocation(a1, 1, [&](auto p4) {
//      rt::store.copy_initialize(a1, p4, p0);
//      std::cout << rt::store.describe_instance(a1, p4) << std::endl;
//      rt::store.deinitialize(a1, p4);
//    });
//
//    // Deinitializes the `List.Cons<Int64>` stoted in `p0`.
//    rt::store.deinitialize(a1, p0);
//  });

  return 0;
}
