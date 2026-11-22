#!/usr/bin/env python3
"""Machine-checked coverage exclusions for libimg2 (stdlib only; run with python3 -I).

Reads the llvm-cov exports written by scripts/coverage.sh (export.json: functions with their code and branch
regions; lcov.info: per-line counts) for src/libimg2 and compares every uncovered item with the manifest
docs/coverage-exclusions.txt (format: see that file's header and docs/coverage.md).

Fails (exit 1) when
  (a) an uncovered line, branch outcome, code region or function is not in the manifest  (UNEXPLAINED)
  (b) a manifest entry is covered now / no longer exists                                   (STALE)
  (c) a manifest anchor differs from the current source line                               (ANCHOR)
  (d) the manifest is malformed (syntax, duplicates, entry without a reason)               (MANIFEST)

Item identity (what an entry names):
  line       a source line with an lcov count of 0 (llvm-cov's own line statistics of the file)
  region     a code region with count 0; detail = start column
  branch-true / branch-false
             a branch condition whose true / false outcome was never taken; detail = start column
  function   a function that never ran; detail = its name
Items inside a macro expansion are located at the expansion site in the function's own file: line = site line,
detail = "<site col>><line>:<col>" of the item inside the macro (a path prefix is added when the macro lives in
another file), so two expansions of one macro are distinct items. Instances of one function or one region
(static inline helpers compiled into several translation units, several test executables) are merged: an item
is covered when any instance covered it. Branches that clang folded to a constant are not items (the same rule
as llvm-cov report: both outcome counters are the constant zero, i.e. the condition's code region ran while
neither outcome was counted).
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict, defaultdict

KINDS = ("line", "region", "branch-true", "branch-false", "function")
LIB_PREFIX = "src/libimg2/"
# the third-party nanosvg headers copied into src/libimg2/decoders are not measured (scripts/coverage.sh IGNORE)
DEFAULT_IGNORE = r"/decoders/nanosvg(rast)?\.h$"

ENTRY_RE = re.compile(r"^(?P<path>[^\s:]+):(?P<line>\d+) (?P<kind>[a-z-]+)(?: (?P<detail>\S+))? = (?P<anchor>.*)$")


class Item:
	__slots__ = ("path", "line", "kind", "detail")

	def __init__(self, path, line, kind, detail):
		self.path, self.line, self.kind, self.detail = path, line, kind, detail

	def key(self):
		return (self.path, self.line, self.kind, self.detail)


def fmt_key(k):
	path, line, kind, detail = k
	return f"{path}:{line} {kind}" + (f" {detail}" if detail else "")


def rel(root, filename):
	p = os.path.normpath(filename)
	r = os.path.normpath(root) + os.sep
	if p.startswith(r):
		p = p[len(r):]
	return p.replace(os.sep, "/")


def short(path):
	return path[len(LIB_PREFIX):] if path.startswith(LIB_PREFIX) else path


# ---------------------------------------------------------------------------------------------------------------
# llvm-cov data


def load_json_items(root, json_path, ignore):
	"""Returns (uncovered region/branch/function keys, totals per kind, per-file totals)."""
	with open(json_path, encoding="utf-8") as fh:
		doc = json.load(fh)
	if doc.get("type") != "llvm.coverage.json.export":
		raise SystemExit(f"coverage_check: {json_path} is not an llvm-cov JSON export")

	regions = defaultdict(int)  # (path, line, detail, span) -> count
	branches = {}  # (path, line, detail, span) -> [true, false, region_ran]
	functions = defaultdict(int)  # (path, line, name) -> count

	for data in doc["data"]:
		for fn in data["functions"]:
			names = fn["filenames"]
			expanded = {r[6] for r in fn["regions"] if r[7] == 1}
			main = 0 if 0 not in expanded else next(i for i in range(len(names)) if i not in expanded)
			main_path = rel(root, names[main])
			if not main_path.startswith(LIB_PREFIX) or ignore.search(main_path):
				continue
			parent = {}
			for r in fn["regions"]:
				if r[7] == 1:  # expansion region: file r[5] expands file r[6] at (r[0], r[1])
					parent[r[6]] = (r[5], r[0], r[1])

			def loc(fid, line, col, depth=0):
				if fid == main or fid not in parent or depth > 64:
					return line, f"{col}"
				pf, pl, pc = parent[fid]
				site_line, site = loc(pf, pl, pc, depth + 1)
				p = rel(root, names[fid])
				inner = f"{line}:{col}" if p == main_path else f"{short(p)}:{line}:{col}"
				return site_line, f"{site}>{inner}"

			# code regions; remember which spans ran (for the folded-branch rule). A function can hold several
			# regions with the same span (e.g. a macro argument expanded twice): they are told apart by their
			# occurrence index, which is the same in every instance of the function.
			ran = defaultdict(int)
			seen = defaultdict(int)
			for r in fn["regions"]:
				if r[7] != 0:
					continue
				line, detail = loc(r[5], r[0], r[1])
				span = (r[5], r[0], r[1], r[2], r[3])
				ident = (names[r[5]],) + span[1:]
				seen[(detail, ident)] += 1
				regions[(main_path, line, detail, ident + (seen[(detail, ident)],))] += r[4]
				ran[span] += r[4]

			for b in fn.get("branches", []):
				line, detail = loc(b[6], b[0], b[1])
				span = (b[6], b[0], b[1], b[2], b[3])
				# the condition ran if a code region with the same span, or (for conditions that are not a
				# region of their own) the innermost enclosing region ran
				cond_ran = ran.get(span, 0) > 0 or enclosing_ran(ran, span)
				ident = (names[b[6]],) + span[1:]
				seen[("branch", detail, ident)] += 1
				k = (main_path, line, detail, ident + (seen[("branch", detail, ident)],))
				e = branches.setdefault(k, [0, 0, False])
				e[0] += b[4]
				e[1] += b[5]
				e[2] = e[2] or cond_ran

			start = fn["regions"][0]
			name = fn["name"].split(":", 1)[1] if ":" in fn["name"] else fn["name"]
			functions[(main_path, start[0], name)] += fn["count"]

	unc = set()
	tot = {k: [0, 0] for k in ("region", "branch", "function")}  # [count, covered]
	per_file = defaultdict(lambda: {k: [0, 0] for k in ("region", "branch", "function")})

	for (path, line, detail, _span), c in regions.items():
		tot["region"][0] += 1
		per_file[path]["region"][0] += 1
		if c > 0:
			tot["region"][1] += 1
			per_file[path]["region"][1] += 1
		else:
			unc.add((path, line, "region", detail))

	for (path, line, detail, _span), (t, f, cond_ran) in branches.items():
		if t == 0 and f == 0 and cond_ran:
			continue  # folded: neither outcome is counted although the condition ran
		for count, kind in ((t, "branch-true"), (f, "branch-false")):
			tot["branch"][0] += 1
			per_file[path]["branch"][0] += 1
			if count > 0:
				tot["branch"][1] += 1
				per_file[path]["branch"][1] += 1
			else:
				unc.add((path, line, kind, detail))

	for (path, line, name), c in functions.items():
		tot["function"][0] += 1
		per_file[path]["function"][0] += 1
		if c > 0:
			tot["function"][1] += 1
			per_file[path]["function"][1] += 1
		else:
			unc.add((path, line, "function", name))
	return unc, tot, per_file


def enclosing_ran(ran, span):
	fid, l0, c0, l1, c1 = span
	best = None
	for (f, a0, b0, a1, b1), c in ran.items():
		if f != fid:
			continue
		if (a0, b0) <= (l0, c0) and (l1, c1) <= (a1, b1):
			size = (a1 - a0, b1 - b0)
			if best is None or size < best[0]:
				best = (size, c)
	return best is not None and best[1] > 0


def load_lcov_lines(root, lcov_path, ignore):
	unc = set()
	tot = [0, 0]
	per_file = defaultdict(lambda: [0, 0])
	path = None
	with open(lcov_path, encoding="utf-8") as fh:
		for raw in fh:
			s = raw.strip()
			if s.startswith("SF:"):
				path = rel(root, s[3:])
				if not path.startswith(LIB_PREFIX) or ignore.search(path):
					path = None
			elif s.startswith("DA:") and path is not None:
				ln, cnt = s[3:].split(",")[:2]
				tot[0] += 1
				per_file[path][0] += 1
				if int(cnt) > 0:
					tot[1] += 1
					per_file[path][1] += 1
				else:
					unc.add((path, int(ln), "line", ""))
			elif s == "end_of_record":
				path = None
	return unc, tot, per_file


# ---------------------------------------------------------------------------------------------------------------
# manifest


def parse_manifest(path):
	entries = OrderedDict()  # key -> (anchor, reason, manifest line)
	errors = []
	reason = None
	reason_line = 0
	reason_used = True
	with open(path, encoding="utf-8") as fh:
		for n, raw in enumerate(fh, 1):
			s = raw.rstrip("\n")
			if not s.strip() or s.lstrip().startswith("#"):
				continue
			if s.startswith("reason:"):
				if not reason_used:
					errors.append(f"{path}:{reason_line}: reason without entries")
				reason = s[len("reason:"):].strip()
				reason_line = n
				reason_used = False
				if not reason:
					errors.append(f"{path}:{n}: empty reason")
				continue
			m = ENTRY_RE.match(s)
			if not m:
				errors.append(f"{path}:{n}: malformed entry: {s}")
				continue
			kind = m.group("kind")
			detail = m.group("detail") or ""
			if kind not in KINDS:
				errors.append(f"{path}:{n}: unknown kind '{kind}'")
				continue
			if (kind == "line") != (detail == ""):
				errors.append(f"{path}:{n}: kind '{kind}' {'takes no' if kind == 'line' else 'needs a'} detail")
				continue
			if reason is None:
				errors.append(f"{path}:{n}: entry before the first reason: line")
				continue
			key = (LIB_PREFIX + m.group("path"), int(m.group("line")), kind, detail)
			if key in entries:
				errors.append(f"{path}:{n}: duplicate entry (first at line {entries[key][2]})")
				continue
			entries[key] = (m.group("anchor"), reason, n)
			reason_used = True
	if not reason_used:
		errors.append(f"{path}:{reason_line}: reason without entries")
	return entries, errors


def source_line(root, path, line, cache):
	if path not in cache:
		try:
			with open(os.path.join(root, path), encoding="utf-8", errors="replace") as fh:
				cache[path] = fh.read().split("\n")
		except OSError:
			cache[path] = None
	lines = cache[path]
	if lines is None or line < 1 or line > len(lines):
		return None
	return lines[line - 1].strip()


def emit_entry(root, key, cache):
	path, line, kind, detail = key
	anchor = source_line(root, path, line, cache) or ""
	return f"{short(path)}:{line} {kind}" + (f" {detail}" if detail else "") + f" = {anchor}"


def pct(c, n):
	return "100.00%" if n == 0 else f"{100.0 * c / n:.2f}%"


# ---------------------------------------------------------------------------------------------------------------


def main():
	ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
	ap.add_argument("--root", required=True, help="repository root")
	ap.add_argument("--json", required=True, help="llvm-cov export (JSON, not -summary-only)")
	ap.add_argument("--lcov", required=True, help="llvm-cov export -format=lcov")
	ap.add_argument("--manifest", required=True, help="exclusion manifest")
	ap.add_argument("--emit-uncovered", action="store_true",
		help="print every uncovered item as a manifest entry (anchor filled in) and exit 0")
	ap.add_argument("--per-file", action="store_true", help="print per-file raw numbers")
	ap.add_argument("--ignore", default=DEFAULT_IGNORE,
		help="regex on repo-relative paths of files not measured (default: %(default)s, as scripts/coverage.sh)")
	a = ap.parse_args()
	root = os.path.abspath(a.root)
	ignore = re.compile(a.ignore)

	unc_j, tot_j, pf_j = load_json_items(root, a.json, ignore)
	unc_l, tot_l, pf_l = load_lcov_lines(root, a.lcov, ignore)
	uncovered = unc_j | unc_l
	cache = {}

	if a.emit_uncovered:
		for k in sorted(uncovered, key=lambda k: (k[0], k[1], KINDS.index(k[2]), k[3])):
			print(emit_entry(root, k, cache))
		return 0

	entries, errors = parse_manifest(a.manifest)
	unexplained = sorted(k for k in uncovered if k not in entries)
	stale = sorted(k for k in entries if k not in uncovered)
	anchors = []
	for k, (anchor, _reason, n) in entries.items():
		cur = source_line(root, k[0], k[1], cache)
		if cur is None:
			anchors.append((n, k, anchor, "<no such line>"))
		elif cur != anchor.strip():
			anchors.append((n, k, anchor, cur))

	if a.per_file:
		print(f"{'file':34} {'lines':>16} {'regions':>16} {'functions':>12} {'branches':>16}")
		for path in sorted(set(pf_l) | set(pf_j)):
			l = pf_l.get(path, [0, 0])
			j = pf_j[path]
			print(f"{short(path):34} {l[1]:>6}/{l[0]:<6} {pct(l[1], l[0]):>7} "
				f"{j['region'][1]:>5}/{j['region'][0]:<5} {j['function'][1]:>4}/{j['function'][0]:<4} "
				f"{j['branch'][1]:>5}/{j['branch'][0]:<5} {pct(j['branch'][1], j['branch'][0])}")

	excl = defaultdict(int)
	for k in entries:
		if k in uncovered:
			excl["branch" if k[2].startswith("branch") else k[2]] += 1

	def line_for(name, c, n):
		e = excl[name]
		return f"  {name:9} raw {pct(c, n):>8} ({n - c} of {n} missed), excluded {e}, after exclusions {pct(c + e, n)}"

	print("coverage_check: src/libimg2 (union over all test executables and instances)")
	print(line_for("line", tot_l[1], tot_l[0]))
	print(line_for("region", tot_j["region"][1], tot_j["region"][0]))
	print(line_for("function", tot_j["function"][1], tot_j["function"][0]))
	print(line_for("branch", tot_j["branch"][1], tot_j["branch"][0]))
	print(f"  manifest: {len(entries)} entries")

	ok = True
	if errors:
		ok = False
		print(f"\nMANIFEST: {len(errors)} error(s)")
		for e in errors:
			print(f"  {e}")
	if unexplained:
		ok = False
		print(f"\nUNEXPLAINED: {len(unexplained)} uncovered item(s) not in the manifest")
		for k in unexplained:
			print(f"  {emit_entry(root, k, cache)}")
	if stale:
		ok = False
		print(f"\nSTALE: {len(stale)} manifest entr{'y' if len(stale) == 1 else 'ies'} covered now (or gone)")
		for k in stale:
			print(f"  {a.manifest}:{entries[k][2]}: {fmt_key((short(k[0]),) + k[1:])}")
	if anchors:
		ok = False
		print(f"\nANCHOR: {len(anchors)} anchor(s) differ from the source")
		for n, k, want, got in anchors:
			print(f"  {a.manifest}:{n}: {fmt_key((short(k[0]),) + k[1:])}\n      manifest: {want}\n      source:   {got}")
	print("\ncoverage_check: " + ("PASS (every uncovered item is a reviewed exclusion)" if ok else "FAIL"))
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
