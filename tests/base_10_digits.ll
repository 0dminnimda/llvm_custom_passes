define dso_local noundef zeroext i8 @base_10_digits(i64 noundef %0) local_unnamed_addr {
  %2 = icmp eq i64 %0, 0
  br i1 %2, label %9, label %3

3:
  %4 = phi i8 [ %7, %3 ], [ 0, %1 ]
  %5 = phi i64 [ %6, %3 ], [ %0, %1 ]
  %6 = udiv i64 %5, 10
  %7 = add i8 %4, 1
  %8 = icmp ult i64 %5, 10
  br i1 %8, label %9, label %3

9:
  %10 = phi i8 [ 0, %1 ], [ %7, %3 ]
  ret i8 %10
}
