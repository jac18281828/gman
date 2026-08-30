/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * AGENTS.md's Gates section claims to be what CI enforces, runnable by
 * hand. Nothing else keeps that claim honest after a workflow edit, so
 * this test does: every job in .github/workflows/ci.yml must be named in
 * AGENTS.md, and the tool vocabulary the Gates block names -- cmake,
 * ctest, valgrind -- must appear on both sides.
 *
 * The `os` dimension of ci.yml's build-job matrix (ubuntu-latest /
 * macos-latest) is deliberately exempt: Gates runs once, locally, on
 * whatever machine the developer has, so there is no OS axis for it to
 * name. Only job identity is checked, not the matrix that produces it.
 *
 * The tool vocabulary below is a fixed constant, not reparsed from
 * AGENTS.md on every run. Deriving it from the very Gates block under test
 * would make the check blind to a wholesale-deleted tool: remove the last
 * "ctest" line and a vocabulary rebuilt from what remains would no longer
 * expect "ctest" anywhere, so the deletion would pass silently. Checking a
 * fixed vocabulary for presence on both sides catches exactly that.
 */

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

bool containsWord(const std::string &text, const std::string &word) {
  std::size_t pos = 0;
  while ((pos = text.find(word, pos)) != std::string::npos) {
    auto isIdentChar = [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    bool leftOk = pos == 0 || !isIdentChar(text[pos - 1]);
    std::size_t end = pos + word.size();
    bool rightOk = end >= text.size() || !isIdentChar(text[end]);
    if (leftOk && rightOk) {
      return true;
    }
    ++pos;
  }
  return false;
}

// Job ids declared directly under ci.yml's top-level `jobs:` key: exactly
// two leading spaces, a bare identifier, and a trailing colon. Anything
// more indented is a step or a matrix entry, not a job.
std::vector<std::string> ciJobNames(const std::string &ciYaml) {
  std::vector<std::string> jobs;
  std::istringstream lines(ciYaml);
  std::string line;
  bool inJobs = false;
  while (std::getline(lines, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!inJobs) {
      if (line == "jobs:") {
        inJobs = true;
      }
      continue;
    }
    if (line.empty()) {
      continue;
    }
    if (line[0] != ' ') {
      break; // back to a top-level key; the jobs: block is over
    }
    if (line.size() > 3 && line[0] == ' ' && line[1] == ' ' &&
        line[2] != ' ' && line.back() == ':') {
      jobs.push_back(line.substr(2, line.size() - 3));
    }
  }
  return jobs;
}

// The whole Gates section, heading to the next `## ` heading: the fenced
// block plus the prose around it that names the CI jobs.
std::string gatesSection(const std::string &agentsMd) {
  std::size_t start = agentsMd.find("## Gates");
  if (start == std::string::npos) {
    return "";
  }
  std::size_t end = agentsMd.find("\n## ", start + 1);
  if (end == std::string::npos) {
    end = agentsMd.size();
  }
  return agentsMd.substr(start, end - start);
}

// The Gates section's fenced shell block: the exact commands a developer
// runs by hand.
std::string gatesBlock(const std::string &agentsMd) {
  std::size_t gatesHeading = agentsMd.find("## Gates");
  if (gatesHeading == std::string::npos) {
    return "";
  }
  std::size_t fenceStart = agentsMd.find("```sh", gatesHeading);
  if (fenceStart == std::string::npos) {
    return "";
  }
  fenceStart = agentsMd.find('\n', fenceStart);
  if (fenceStart == std::string::npos) {
    return "";
  }
  ++fenceStart;
  std::size_t fenceEnd = agentsMd.find("```", fenceStart);
  if (fenceEnd == std::string::npos) {
    return "";
  }
  return agentsMd.substr(fenceStart, fenceEnd - fenceStart);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <AGENTS.md> <ci.yml>\n", argv[0]);
    return 2;
  }

  const std::string agentsMd = readFile(argv[1]);
  const std::string ciYaml = readFile(argv[2]);
  check(!agentsMd.empty(), "AGENTS.md read");
  check(!ciYaml.empty(), "ci.yml read");

  const std::vector<std::string> jobs = ciJobNames(ciYaml);
  check(jobs.size() >= 3, "ci.yml has at least the three known jobs");

  // Backtick-delimited, and only within the Gates section -- not a bare
  // substring search over the whole file. AGENTS.md is prose about a build
  // system, so most job names a real workflow would use ("test", "build",
  // "commits") already occur somewhere in it as ordinary English or as
  // part of a longer command; a loose search reports those as named and
  // exits 0 on a job nobody documented. The Gates section writes each job
  // name in backticks, so requiring that exact form is what makes the
  // check mean "documented here" rather than "these letters appear".
  const std::string section = gatesSection(agentsMd);
  check(!section.empty(), "AGENTS.md has a Gates section");
  for (const auto &job : jobs) {
    check(section.find("`" + job + "`") != std::string::npos,
          "ci.yml job \"" + job + "\" is named in AGENTS.md's Gates section");
  }

  const std::string gates = gatesBlock(agentsMd);
  check(!gates.empty(), "AGENTS.md's Gates section has a fenced shell block");

  // The vocabulary the Gates block names as of this writing -- see the
  // file comment above for why it is a fixed constant.
  const std::vector<std::string> vocabulary = {"cmake", "ctest", "valgrind"};
  for (const auto &tool : vocabulary) {
    check(containsWord(gates, tool),
          "Gates block still names \"" + tool + "\"");
    check(containsWord(ciYaml, tool),
          "ci.yml still runs \"" + tool + "\"");
  }

  return checkSummary("AGENTS.md tracks ci.yml");
}
