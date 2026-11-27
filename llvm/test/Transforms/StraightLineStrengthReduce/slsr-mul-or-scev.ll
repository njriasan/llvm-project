; RUN: opt -disable-output -passes='print<scalar-evolution>,slsr,print<scalar-evolution>' < %s 2>&1 | FileCheck %s

declare void @sink(i8)

; Reusing the multiply replaces the OR with wrapping addition. The scalar
; extract can then be zero, even though SCEV cannot follow that vector use.
define noundef i8 @or_range_through_vector(i8 noundef %x, i8 noundef %s) {
; CHECK-LABEL: Classifying expressions for: @or_range_through_vector
; CHECK: %o = or disjoint i8 %x, 4
; CHECK: %e = extractelement
; CHECK-NEXT: -->  %e U: [4,0) S: [4,0)
; CHECK-LABEL: Classifying expressions for: @or_range_through_vector
; CHECK: %o = add i8 %x, 4
; CHECK: %e = extractelement
; CHECK-NEXT: -->  %e U: full-set S: full-set
  %o = or disjoint i8 %x, 4
  %v = insertelement <2 x i8> zeroinitializer, i8 %o, i32 0
  %e = extractelement <2 x i8> %v, i32 0
  %fe = freeze i8 %e
  call void @sink(i8 %fe)
  %m0 = mul i8 %o, %s
  %fm = freeze i8 %m0
  call void @sink(i8 %fm)
  %sum = add i8 %x, 5
  %m1 = mul i8 %sum, %s
  ret i8 %m1
}

; Rewriting the multiply can also relax a disjoint OR on the candidate side
; while the basis remains unchanged.
define i8 @candidate_or_range_through_vector(i8 noundef %x, i8 noundef %u) {
; CHECK-LABEL: Classifying expressions for: @candidate_or_range_through_vector
; CHECK: %m1 = mul i8 %or, %s
; CHECK: %e = extractelement
; CHECK-NEXT: -->  %e U: [1,0) S: [1,0)
; CHECK-LABEL: Classifying expressions for: @candidate_or_range_through_vector
; CHECK: %m1 = add i8 %m0, %s
; CHECK: %e = extractelement
; CHECK-NEXT: -->  %e U: full-set S: full-set
  %a = and i8 %x, 1
  %s = or i8 %u, 1
  %m0 = mul i8 %a, %s
  %or = or disjoint i8 %a, 1
  %m1 = mul i8 %or, %s
  %v = insertelement <2 x i8> zeroinitializer, i8 %m1, i32 0
  %e = extractelement <2 x i8> %v, i32 0
  %fe = freeze i8 %e
  ret i8 %fe
}
