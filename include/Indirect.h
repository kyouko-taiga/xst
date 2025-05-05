#pragma once

#include <utility>

/// A box around an object stored out of line.
template<typename T>
struct Indirect {
private:

  /// The wrapped object.
  T* wrapped;

public:

  /// Creates an instance taking ownership of the given pointer.
  Indirect(T* wrapped) : wrapped(wrapped) {}

  Indirect(Indirect const& other) : wrapped(new T(*other)) {}

  Indirect(Indirect&& other) : wrapped(new T{std::move(*other.wrapped)}) {}

  Indirect& operator=(Indirect& other) {
    *wrapped = *other;
    return *this;
  }

  Indirect& operator=(Indirect&& other) {
    *wrapped = std::move(*other);
    return *this;
  }

  /// Destroys `this` and the object that it wraps.
  ~Indirect() {
    delete wrapped;
  }

  /// Accesses the wrapped object.
  inline T& operator*() { return *wrapped; }

  /// Accesses the wrapped object.
  inline T const& operator*() const { return *wrapped; }

  /// Accesses the wrapped object.
  inline T* operator->() { return wrapped; }

  /// Accesses the wrapped object.
  inline T const* operator->() const { return wrapped; }

};

/// Creates an indirect box wrapping an object constructed with the given arguments.
template<typename T, typename... Us>
Indirect<T> make_indirect(Us&&... arguments) {
  return Indirect<T>{new T{std::forward<Us>(arguments)...}};
}
