// RUN: %clang_cc1 -std=c++17 -fsyntax-only -verify %s
// RUN: not %clang_cc1 -std=c++17 -fsyntax-only %s 2>&1 | FileCheck %s

// Candidates for a call without an object argument ignore the object
// argument slot. Only that slot may be skipped when ordering the candidates
// for display; the remaining conversions have to be compared on both sides.

namespace free_functions {
void h(int *, float); // expected-note {{candidate function not viable}}
void h(int *, char);  // expected-note {{candidate function not viable}}

void test() {
  h(1, 'a'); // expected-error {{no matching function for call to 'h'}}
  // CHECK: error: no matching function for call to 'h'
  // CHECK: note: candidate function not viable
  // CHECK-NEXT: void h(int *, char);
  // CHECK: note: candidate function not viable
  // CHECK-NEXT: void h(int *, float);
}
} // namespace free_functions

namespace static_members {
struct S {
  static void g(int *, float); // expected-note {{candidate function not viable}}
  static void g(int *, char);  // expected-note {{candidate function not viable}}
};

void test() {
  S::g(1, 'a'); // expected-error {{no matching function for call to 'g'}}
  // CHECK: error: no matching function for call to 'g'
  // CHECK: note: candidate function not viable
  // CHECK-NEXT: static void g(int *, char);
  // CHECK: note: candidate function not viable
  // CHECK-NEXT: static void g(int *, float);
}
} // namespace static_members

namespace static_member_template {
// A static member function template whose deduction succeeds but whose
// non-dependent conversion fails used to read an uninitialized object
// argument conversion while the candidates were sorted.
struct S {
  static void g(int *); // expected-note {{candidate function not viable}}
  template <class U = int>
  static void g(int *); // expected-note {{candidate function template not viable}}
};

void test() {
  S::g(1); // expected-error {{no matching function for call to 'g'}}
}

template <class T> T &&declval();

template <class, class T, class... A> struct has_g {
  static constexpr bool value = false;
};
template <class T, class... A>
struct has_g<decltype(void(T::g(declval<A>()...))), T, A...> {
  static constexpr bool value = true;
};

static_assert(!has_g<void, S, int>::value, "");
static_assert(has_g<void, S, int *>::value, "");
} // namespace static_member_template
