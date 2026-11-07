"""Builds results.xlsx (data + chart + answers) from part2_2_median.csv
written by Part2_2.c.

Usage: python3 make_excel.py
"""
import csv

from openpyxl import Workbook
from openpyxl.chart import Reference, ScatterChart, Series
from openpyxl.chart.axis import ChartLines
from openpyxl.chart.data_source import NumFmt
from openpyxl.chart.shapes import GraphicalProperties
from openpyxl.drawing.line import LineProperties
from openpyxl.styles import Font, PatternFill

FUNCS = ["sequential", "pipes", "thread", "mmap"]
COLORS = {"sequential": "F6B26B", "pipes": "4A86E8", "thread": "6AA84F", "mmap": "CC0000"}
WIN_FILL = PatternFill("solid", fgColor="FFF2CC")
HEADERS = ["N", "n_proc", "repetitions"] + [f"{f}_median_seconds" for f in FUNCS] + ["fastest"]


def read_medians(path):
    with open(path, newline="") as f:
        return [
            {
                "N": int(r["N"]),
                "n_proc": int(r["n_proc"]),
                "repetitions": int(r["repetitions"]),
                **{fn: float(r[f"{fn}_median_seconds"]) for fn in FUNCS},
            }
            for r in csv.DictReader(f)
        ]


def fastest(r):
    return min(FUNCS, key=lambda fn: r[fn])


def winning_ranges(rows, func):
    """Contiguous ranges of n where `func` is the fastest of the 4."""
    ranges, start, prev = [], None, None
    for r in rows:
        if fastest(r) == func:
            start = r["n_proc"] if start is None else start
            prev = r["n_proc"]
        elif start is not None:
            ranges.append((start, prev))
            start = None
    if start is not None:
        ranges.append((start, prev))
    return ranges


def style_axis(axis, title, fmt, major_unit=None, lo=None, hi=None):
    axis.title = title
    axis.title.overlay = False
    axis.delete = False
    axis.scaling.min, axis.scaling.max = lo, hi
    if major_unit:
        axis.majorUnit = major_unit
    axis.numFmt = NumFmt(formatCode=fmt, sourceLinked=False)
    axis.crosses = "min"
    axis.tickLblPos = "nextTo"
    axis.majorGridlines = ChartLines(spPr=GraphicalProperties(ln=LineProperties(solidFill="E0E0E0")))


def add_chart(ws, rows):
    chart = ScatterChart()
    chart.title = f"Time vs n_proc / n_thread (N = {rows[0]['N']:,})"
    chart.title.overlay = False
    chart.height, chart.width = 13, 24
    chart.legend.position = "t"
    chart.legend.overlay = False
    style_axis(chart.x_axis, "n_proc / n_thread", "0", major_unit=16, lo=0, hi=rows[-1]["n_proc"])
    style_axis(chart.y_axis, "Time (s, median)", "0.000", lo=0)

    xs = Reference(ws, min_col=2, min_row=4, max_row=3 + len(rows))
    for i, fn in enumerate(FUNCS):
        ys = Reference(ws, min_col=4 + i, min_row=4, max_row=3 + len(rows))
        s = Series(ys, xs, title=f"{fn}_compute")
        s.marker.symbol = "none"
        s.smooth = False  # smoothing comes from the median; Excel smoothing overshoots between points
        s.graphicalProperties.line.solidFill = COLORS[fn]
        s.graphicalProperties.line.width = 28575  # 2.25 pt
        chart.series.append(s)
    ws.add_chart(chart, "J3")


def main():
    rows = read_medians("part2_2_median.csv")

    wb = Workbook()
    ws = wb.active
    ws.title = "Fixed N - vary n"
    ws.append([f"Fixed N = {rows[0]['N']:,}, n = "
               + ", ".join(str(r["n_proc"]) for r in rows[:4])
               + f", ..., {rows[-1]['n_proc']}, median of {rows[0]['repetitions']} readings"])
    ws["A1"].font = Font(bold=True, size=13)
    ws.append([])
    ws.append(HEADERS)
    for cell in ws[3]:
        cell.font = Font(bold=True)

    for r in rows:
        ws.append([r["N"], r["n_proc"], r["repetitions"]] + [r[fn] for fn in FUNCS] + [fastest(r)])
    # highlighting the fastest cell of every row
    for i, r in enumerate(rows):
        ws.cell(row=4 + i, column=4 + FUNCS.index(fastest(r))).fill = WIN_FILL
    for row in ws.iter_rows(min_row=4, min_col=4, max_col=3 + len(FUNCS)):
        for cell in row:
            cell.number_format = "0.000000000"
    for col, width in zip("ABCDEFGH", [12, 6, 12, 26, 22, 22, 22, 12]):
        ws.column_dimensions[col].width = width

    # answers
    counts = {fn: sum(fastest(r) == fn for r in rows) for fn in FUNCS}
    overall = max(FUNCS, key=lambda fn: counts[fn])
    summary = [f"Fastest overall: {overall}_compute (fastest at {counts[overall]} of {len(rows)} values of n)"]
    for fn in FUNCS:
        ranges = winning_ranges(rows, fn)
        if ranges:
            summary.append(f"{fn}_compute is the fastest for n in: "
                           + ", ".join(f"[{lo}, {hi}]" for lo, hi in ranges))
    for fn in FUNCS[1:]:
        best = min(rows, key=lambda r: r[fn])
        summary.append(f"Best {fn}_compute: n = {best['n_proc']} ({best[fn]:.6f} s, "
                       f"speedup {best['sequential'] / best[fn]:.2f}x vs sequential)")

    summary_row = 4 + len(rows) + 1
    for k, line in enumerate(summary):
        ws.cell(row=summary_row + k, column=1, value=line).font = Font(bold=True)
        print(line)

    add_chart(ws, rows)
    wb.save("results.xlsx")
    print("Saved results.xlsx")


if __name__ == "__main__":
    main()
