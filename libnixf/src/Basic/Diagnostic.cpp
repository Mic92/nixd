#include "nixf/Basic/Diagnostic.h"

#include <sstream>

using namespace nixf;

namespace {

std::string simpleFormat(const char *Fmt,
                         const std::vector<std::string> &Args) {
  std::stringstream SS;
  std::size_t ArgIdx = 0;
  for (const char *Cur = Fmt; *Cur;) {
    if (*Cur == '{' && *(Cur + 1) == '}') {
      SS << Args[ArgIdx++];
      Cur += 2;
    } else {
      SS << *Cur;
      ++Cur;
    }
  }
  return SS.str();
}

} // namespace

const char *nixf::Note::sname(NoteKind Kind) {
  switch (Kind) {
#define DIAG_NOTE(SNAME, CNAME, MESSAGE)                                       \
  case NK_##CNAME:                                                             \
    return SNAME;
#include "nixf/Basic/NoteKinds.inc"
#undef DIAG_NOTE
  }
  assert(false && "Invalid diagnostic kind");
  __builtin_unreachable();
}

nixf::Diagnostic::Severity nixf::Diagnostic::severity(DiagnosticKind Kind) {
  switch (Kind) {
#define DIAG(SNAME, CNAME, SEVERITY, MESSAGE)                                  \
  case DK_##CNAME:                                                             \
    return DS_##SEVERITY;
#include "nixf/Basic/DiagnosticKinds.inc"
#undef DIAG
  }
  assert(false && "Invalid diagnostic kind");
  __builtin_unreachable();
}
const char *nixf::Diagnostic::message(DiagnosticKind Kind) {
  switch (Kind) {
#define DIAG(SNAME, CNAME, SEVERITY, MESSAGE)                                  \
  case DK_##CNAME:                                                             \
    return MESSAGE;
#include "nixf/Basic/DiagnosticKinds.inc"
#undef DIAG
  }
  assert(false && "Invalid diagnostic kind");
  __builtin_unreachable();
}
const char *nixf::Diagnostic::sname(DiagnosticKind Kind) {
  switch (Kind) {
#define DIAG(SNAME, CNAME, SEVERITY, MESSAGE)                                  \
  case DK_##CNAME:                                                             \
    return SNAME;
#include "nixf/Basic/DiagnosticKinds.inc"
#undef DIAG
  }
  assert(false && "Invalid diagnostic kind");
  __builtin_unreachable();
}
const char *nixf::Note::message(NoteKind Kind) {
  switch (Kind) {
#define DIAG_NOTE(SNAME, CNAME, MESSAGE)                                       \
  case NK_##CNAME:                                                             \
    return MESSAGE;
#include "nixf/Basic/NoteKinds.inc"
#undef DIAG_NOTE
  }
  assert(false && "Invalid diagnostic kind");
  __builtin_unreachable();
}

std::string PartialDiagnostic::format() const {
  return simpleFormat(message(), Args);
}
