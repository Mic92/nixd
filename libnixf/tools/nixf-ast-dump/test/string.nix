# RUN: nixf-ast-dump < %s | FileCheck %s

# CHECK:      String
# CHECK:      Token 9 asdasdasd
# CHECK-NEXT: Interpolation 11
# CHECK-NEXT:  Token 2 ${
# CHECK-NEXT:  String 8
# CHECK-NEXT:   Token 1  "
# CHECK-NEXT:   Token 6 string
# CHECK-NEXT:   Token 1 "
# CHECK-NEXT:  Token 1  }
# CHECK-NEXT: Token 6 asdasd
# CHECK-NEXT: Token 1 "
"asdasdasd${ "string" }asdasd"
