#!/usr/bin/env python3
"""Run the auth suites and report GCC line/branch coverage for four auth sources.

Requires Python 3.9+, an existing coverage-enabled CMake build, and matching gcov.
Uses only Python's standard library. Output contains no application database.
"""

import argparse
import gzip
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone


AUTH_SOURCES = (
    "src/auth.cpp",
    "src/password_hasher.cpp",
    "src/auth_event_log.cpp",
    "src/sqlite_user_repository.cpp",
)
TEST_FILTER = "Auth.*:PasswordHasher.*:AuthEventLog.*:SqliteUserRepository.*:AuthRoleBoundary.*"


def run(command, cwd, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            encoding="utf-8", errors="replace", capture_output=True)
    return result, result.stdout + result.stderr


def percent(covered, total):
    return round(100.0 * covered / total, 2) if total else None


def git_blob_sha(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=Path("."))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument("--revision", required=True, help="Base commit; local source hashes are also recorded")
    args = parser.parse_args()
    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if "QUIZSPHERE_COVERAGE:BOOL=ON" not in cache:
        parser.error("Configure this build with -DQUIZSPHERE_COVERAGE=ON first")
    executable = build / ("auth_tests.exe" if os.name == "nt" else "auth_tests")
    if not executable.is_file():
        parser.error("Build the auth_tests target first")
    output.mkdir(parents=True, exist_ok=True)
    # Counters must represent this run only. Never follow a counter outside the build.
    for counter in build.rglob("*.gcda"):
        if build not in counter.resolve().parents:
            raise RuntimeError("Coverage counter resolves outside the selected build")
        counter.unlink()

    with tempfile.TemporaryDirectory(prefix="quizsphere-auth-coverage-") as scratch:
        env = os.environ.copy()
        env["TEMP"] = scratch
        env["TMP"] = scratch
        env["TMPDIR"] = scratch
        xml_path = Path(scratch) / "auth-results.xml"
        result, test_output = run(
            [str(executable), "--gtest_filter=" + TEST_FILTER,
             "--gtest_output=xml:" + str(xml_path)], scratch, env)
        (output / "auth-tests.txt").write_text(test_output, encoding="utf-8")
        if result.returncode:
            raise RuntimeError("Auth tests failed; see auth-tests.txt")
        test_exit_code = result.returncode
        test_xml = ET.parse(xml_path).getroot()
        test_counts = {"tests": int(test_xml.get("tests", 0)),
                       "failures": int(test_xml.get("failures", 0)),
                       "skipped": len(test_xml.findall(".//testcase/skipped"))}

    gcov_version_result, gcov_version = run([args.gcov, "--version"], build)
    if gcov_version_result.returncode:
        raise RuntimeError(gcov_version)
    reports = []
    gcov_log = []
    gcov_dir = output / "raw-gcov"
    gcov_dir.mkdir(exist_ok=True)
    for filename in AUTH_SOURCES:
        basename = Path(filename).name
        candidates = list(build.rglob(basename + ".gcda")) + list(build.rglob(basename + ".obj.gcda"))
        candidates += list(build.rglob(basename + ".o.gcda"))
        candidates = list(dict.fromkeys(candidates))
        if len(candidates) != 1:
            raise RuntimeError("Expected exactly one coverage counter for " + filename)
        counter = candidates[0]
        result, log = run([args.gcov, "--json-format", "--branch-probabilities",
                           "--branch-counts", str(counter)], gcov_dir)
        gcov_log.append(log)
        if result.returncode:
            raise RuntimeError(log)
        json_path = gcov_dir / (counter.name.removesuffix(".gcda") + ".gcov.json.gz")
        with gzip.open(json_path, "rt", encoding="utf-8") as stream:
            document = json.load(stream)
        selected = []
        for entry in document["files"]:
            entry_path = Path(entry["file"])
            if not entry_path.is_absolute():
                entry_path = Path(document["current_working_directory"]) / entry_path
            if entry_path.resolve() == (source / filename).resolve():
                selected.append(entry)
        if len(selected) != 1:
            raise RuntimeError("Cannot identify source coverage for " + filename)
        lines = selected[0]["lines"]
        branches = [branch for line in lines for branch in line.get("branches", [])]
        line_counts = {}
        for line in lines:
            number = line["line_number"]
            line_counts[number] = line_counts.get(number, 0) + line["count"]
        total_lines = len(line_counts)
        covered_lines = sum(count > 0 for count in line_counts.values())
        covered_branches = sum(branch["count"] > 0 for branch in branches)
        data = (source / filename).read_bytes()
        reports.append({
            "file": filename, "source_git_blob_sha": git_blob_sha(data),
            "lines_covered": covered_lines, "lines_total": total_lines,
            "line_percent": percent(covered_lines, total_lines),
            "branches_covered": covered_branches, "branches_total": len(branches),
            "branch_percent": percent(covered_branches, len(branches)),
            "uncovered_lines": sorted(number for number, count in line_counts.items() if count == 0),
            "uncovered_branch_lines": sorted({line["line_number"] for line in lines
                                               if any(b["count"] == 0 for b in line.get("branches", []))}),
        })

    totals = {key: sum(item[key] for item in reports) for key in
              ("lines_covered", "lines_total", "branches_covered", "branches_total")}
    totals["line_percent"] = percent(totals["lines_covered"], totals["lines_total"])
    totals["branch_percent"] = percent(totals["branches_covered"], totals["branches_total"])
    summary = {
        "base_commit": args.revision,
        "executed_at_utc": datetime.now(timezone.utc).isoformat(),
        "scope": list(AUTH_SOURCES), "test_filter": TEST_FILTER,
        "test_exit_code": test_exit_code, "test_counts": test_counts,
        "branch_policy": "Every GCC-reported branch, including exception/compiler-generated branches; covered means taken at least once",
        "gcov_version": gcov_version.splitlines()[0],
        "totals": totals, "files": reports,
    }
    (output / "coverage-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (output / "gcov-output.txt").write_text("\n".join(gcov_log), encoding="utf-8")
    rows = "".join(
        "<tr><td>" + html.escape(item["file"]) + "</td><td>"
        + f'{item["lines_covered"]}/{item["lines_total"]} ({item["line_percent"]}%)'
        + "</td><td>" + f'{item["branches_covered"]}/{item["branches_total"]} ({item["branch_percent"]}%)'
        + "</td></tr>" for item in reports)
    missing = "".join("<h2>" + html.escape(item["file"]) + "</h2><p>Uncovered lines: "
                      + html.escape(str(item["uncovered_lines"])) + "</p><p>Lines with uncovered branches: "
                      + html.escape(str(item["uncovered_branch_lines"])) + "</p>" for item in reports)
    page = ("<!doctype html><html lang='en'><meta charset='utf-8'><title>QuizSphere auth coverage</title>"
            "<style>body{font:16px system-ui;max-width:1050px;margin:40px auto;padding:0 20px;line-height:1.5}"
            "table{border-collapse:collapse;width:100%}th,td{padding:12px;border:1px solid #bbb;text-align:left}"
            "h2{font-size:18px}p{overflow-wrap:anywhere}</style><h1>Authentication line and branch coverage</h1>"
            "<p>Base commit: <code>" + html.escape(args.revision) + "</code>. Source hashes and counters are in "
            "<a href='coverage-summary.json'>coverage-summary.json</a>.</p><p>Auth-only execution, four production "
            "sources. Every GCC-reported branch is included, including exception/compiler-generated branches. "
            "Coverage measures execution; it is not proof of correctness or full product completion.</p>"
            "<table><tr><th>Source</th><th>Lines executed</th><th>Branches taken</th></tr>" + rows
            + "<tr><th>Total</th><th>" + f'{totals["lines_covered"]}/{totals["lines_total"]} ({totals["line_percent"]}%)'
            + "</th><th>" + f'{totals["branches_covered"]}/{totals["branches_total"]} ({totals["branch_percent"]}%)'
            + "</th></tr></table>" + missing + "</html>")
    (output / "coverage.html").write_text(page, encoding="utf-8")
    print(json.dumps(totals, indent=2))
    print("Reports: " + str(output))


if __name__ == "__main__":
    main()
