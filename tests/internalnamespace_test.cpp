/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/*
 * Holds the rule that every class, struct, enum, free function and
 * typedef/alias a libgman/ header declares lives in namespace gman. Reads
 * each header's source at run time rather than its compiled symbols, so
 * the test itself needs no rebuild to catch a declaration moved back out
 * -- only the moved-back tree would fail to compile, and that is not the
 * failure this test proves.
 *
 * The scan tracks brace nesting to know, at each declaration, whether its
 * enclosing scope is namespace gman, some other namespace, a class or
 * struct body (whose members are never a concern of their own), or
 * global scope. A forward declaration (a bare "class X;" or "struct X;")
 * never defines anything, so it passes anywhere -- struct tiff in
 * gmantiff.h relies on exactly that. Comments, string and character
 * literals are blanked before scanning so a brace or semicolon mentioned
 * in prose never perturbs the nesting count; preprocessor lines are
 * skipped outright.
 */

#include <cctype>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "check.h"
#include "sourcescan.h"

namespace {

namespace fs = std::filesystem;

// Blanks a // comment starting at out[i], leaving its terminating newline
// (if any) alone so line numbers stay meaningful. Leaves i just past it.
void blankLineComment(std::string& out, std::size_t& i) {
  const std::size_t n = out.size();
  while (i < n && out[i] != '\n') {
    out[i] = ' ';
    ++i;
  }
}

// Blanks a /* */ comment starting at out[i], preserving embedded newlines.
// Leaves i just past the closing */, or at n if the comment is unterminated.
void blankBlockComment(std::string& out, std::size_t& i) {
  const std::size_t n = out.size();
  out[i] = ' ';
  out[i + 1] = ' ';
  i += 2;
  while (i + 1 < n && !(out[i] == '*' && out[i + 1] == '/')) {
    if (out[i] != '\n') {
      out[i] = ' ';
    }
    ++i;
  }
  if (i + 1 < n) {
    out[i] = ' ';
    out[i + 1] = ' ';
    i += 2;
  }
}

// Blanks a "..." or '...' literal starting at out[i]. Leaves i just past
// its closing quote, or at n if the literal is unterminated.
void blankLiteral(std::string& out, std::size_t& i) {
  const std::size_t n = out.size();
  const char quote = out[i];
  out[i] = ' ';
  ++i;
  while (i < n && out[i] != quote) {
    if (out[i] == '\\' && i + 1 < n) {
      out[i] = ' ';
      ++i;
    }
    if (i < n && out[i] != '\n') {
      out[i] = ' ';
    }
    ++i;
  }
  if (i < n) {
    out[i] = ' ';
    ++i;
  }
}

// Blanks // and /* */ comments and the bodies of string/character literals,
// preserving every other character (including newlines, so line numbers
// stay meaningful) and length, so brace/semicolon offsets into the
// returned string still index the same characters as the original.
std::string stripCommentsAndLiterals(std::string const& text) {
  std::string out = text;
  const std::size_t n = out.size();
  std::size_t i = 0;
  while (i < n) {
    if (out[i] == '/' && i + 1 < n && out[i + 1] == '/') {
      blankLineComment(out, i);
      continue;
    }
    if (out[i] == '/' && i + 1 < n && out[i + 1] == '*') {
      blankBlockComment(out, i);
      continue;
    }
    if (out[i] == '"' || out[i] == '\'') {
      blankLiteral(out, i);
      continue;
    }
    ++i;
  }
  return out;
}

// Blanks every preprocessor line (one starting, after leading whitespace,
// with '#'), so #include/#pragma/#define never reach the brace/semicolon
// scan below.
std::string stripPreprocessorLines(std::string const& text) {
  std::string out = text;
  std::size_t lineStart = 0;
  while (lineStart < out.size()) {
    std::size_t lineEnd = out.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = out.size();
    }
    std::size_t firstNonSpace = lineStart;
    while (firstNonSpace < lineEnd && std::isspace(static_cast<unsigned char>(out[firstNonSpace]))) {
      ++firstNonSpace;
    }
    if (firstNonSpace < lineEnd && out[firstNonSpace] == '#') {
      for (std::size_t j = lineStart; j < lineEnd; ++j) {
        out[j] = ' ';
      }
    }
    lineStart = lineEnd + 1;
  }
  return out;
}

std::string trim(std::string const& s) {
  std::size_t begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  std::size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

enum class ScopeKind { GmanNamespace, OtherNamespace, ClassOrStruct, EnumDef, Other };

const std::regex kGmanNamespace(R"(^namespace\s+gman$)");
const std::regex kOtherNamespace(R"(^namespace(\s+\w+)?$)");
const std::regex kEnumDef(R"(^(typedef\s+)?enum(\s+class)?(\s+\w+)?$)");
const std::regex kClassOrStruct(R"(\b(class|struct)\s+\w+(\s*:\s*[^{]*)?$)");
const std::regex kForwardDecl(R"(^(class|struct)\s+\w+$)");
const std::regex kTypedefOrAlias(R"(^typedef\b|^using\s+\w+\s*=)");

ScopeKind classifyOpeningBrace(std::string const& prefix) {
  if (std::regex_search(prefix, kGmanNamespace)) {
    return ScopeKind::GmanNamespace;
  }
  if (std::regex_search(prefix, kOtherNamespace)) {
    return ScopeKind::OtherNamespace;
  }
  if (std::regex_search(prefix, kEnumDef)) {
    return ScopeKind::EnumDef;
  }
  if (std::regex_search(prefix, kClassOrStruct)) {
    return ScopeKind::ClassOrStruct;
  }
  return ScopeKind::Other;
}

// True when a class/struct/enum opened with this stack on top must live in
// namespace gman: the enclosing scope is global (empty stack) or some
// other, non-gman namespace -- the only scopes namespace gman still needs
// wrapped around a definition.
bool requiresGmanNamespace(std::vector<ScopeKind> const& stack) {
  return stack.empty() || stack.back() == ScopeKind::OtherNamespace;
}

// Classifies the brace opening at text[i], records a violation if it opens
// a class/struct/enum outside namespace gman, and pushes its ScopeKind.
void recordOpenBrace(std::string const& text, std::size_t i, std::size_t lastBoundary, std::vector<ScopeKind>& stack,
                     std::vector<std::string>& violations) {
  const std::string prefix = trim(text.substr(lastBoundary, i - lastBoundary));
  const ScopeKind kind = classifyOpeningBrace(prefix);
  if ((kind == ScopeKind::ClassOrStruct || kind == ScopeKind::EnumDef) && requiresGmanNamespace(stack)) {
    violations.push_back("defines \"" + prefix + "\" outside namespace gman");
  }
  stack.push_back(kind);
}

// Classifies the statement ending at a ';' at namespace (or global) scope,
// recording a violation for a typedef/alias or free-function declaration
// outside namespace gman. A forward declaration defines nothing and passes
// anywhere.
void recordStatementViolation(std::string const& stmt, ScopeKind current, std::vector<std::string>& violations) {
  if (std::regex_match(stmt, kForwardDecl)) {
    return;
  }
  if (current == ScopeKind::GmanNamespace) {
    return;
  }
  if (std::regex_search(stmt, kTypedefOrAlias)) {
    violations.push_back("declares a typedef/alias outside namespace gman: \"" + stmt + "\"");
  } else if (stmt.find('(') != std::string::npos) {
    violations.push_back("declares a free function outside namespace gman: \"" + stmt + "\"");
  }
}

// Scans one header's already-blanked text, reporting every class, struct,
// enum, free function or typedef/alias declared outside namespace gman.
// An empty scope stack means global scope; it is classified as
// ScopeKind::OtherNamespace below, since neither is namespace gman and the
// check treats them alike.
std::vector<std::string> violationsIn(std::string const& text) {
  std::vector<std::string> violations;
  std::vector<ScopeKind> stack;
  std::size_t lastBoundary = 0;

  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '{') {
      recordOpenBrace(text, i, lastBoundary, stack, violations);
      lastBoundary = i + 1;
    } else if (c == '}') {
      if (!stack.empty()) {
        stack.pop_back();
      }
      lastBoundary = i + 1;
    } else if (c == ';') {
      const std::string stmt = trim(text.substr(lastBoundary, i - lastBoundary));
      const ScopeKind current = stack.empty() ? ScopeKind::OtherNamespace : stack.back();
      // Only a declaration directly at namespace (or global) scope is one
      // of ours to check -- a statement nested in a class body, an enum's
      // enumerator list or a function body (an inline method's, most
      // often) names calls and locals that are none of this test's
      // business.
      const bool atNamespaceLevel =
          stack.empty() || current == ScopeKind::GmanNamespace || current == ScopeKind::OtherNamespace;
      if (atNamespaceLevel && !stmt.empty()) {
        recordStatementViolation(stmt, current, violations);
      }
      lastBoundary = i + 1;
    }
  }
  return violations;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <source-dir> [source-dir ...]\n";
    return 2;
  }

  std::vector<std::string> dirs;
  for (int i = 1; i < argc; ++i) {
    dirs.push_back(argv[i]);
  }

  const std::vector<fs::path> files = collectSourceFiles(dirs);
  int headerCount = 0;
  for (fs::path const& file : files) {
    if (file.extension() == ".h") {
      ++headerCount;
      const std::string raw = readFile(file);
      const std::string blanked = stripCommentsAndLiterals(stripPreprocessorLines(raw));
      const std::vector<std::string> violations = violationsIn(blanked);
      for (auto const& violation : violations) {
        check(false, file.generic_string() + ": " + violation);
      }
      if (violations.empty()) {
        check(true, file.generic_string() + ": every declaration lives in namespace gman");
      }
    }
  }

  check(headerCount >= 20, "scanned at least 20 headers (found " + std::to_string(headerCount) + ")");

  return checkSummary("every libgman/ declaration lives in namespace gman");
}
