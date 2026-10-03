"""<Name>_registers.html: a self-contained, searchable register table with a Modbus view."""

from html import escape

from regmap.codes import Access, RegType
from regmap.resolve import ResolvedBlock, ResolvedMap, ResolvedRegister

STYLE = """\
:root {
  --bg: #ffffff; --fg: #1f2328; --muted: #656d76; --line: #d0d7de; --head: #f6f8fa;
  --accent: #0b5cad; --chip: #eaeef2;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0d1117; --fg: #e6edf3; --muted: #8b949e; --line: #30363d; --head: #161b22;
    --accent: #58a6ff; --chip: #21262d;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg);
  font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
header, main { padding: 0 16px; max-width: 1400px; margin: 0 auto; }
h1 { margin: 16px 0 4px; font-size: 22px; }
h2 { margin: 28px 0 6px; font-size: 17px; }
h3 { margin: 16px 0 6px; font-size: 15px; }
.sub { color: var(--muted); font-weight: normal; }
p.sub { margin: 0 0 12px; }
.bar { position: sticky; top: 0; z-index: 2; background: var(--bg);
  border-bottom: 1px solid var(--line); padding: 8px 16px;
  display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.bar input, .bar select { font: inherit; padding: 4px 8px; color: var(--fg);
  background: var(--bg); border: 1px solid var(--line); border-radius: 6px; }
.bar input[type=search] { min-width: 220px; }
nav { display: flex; flex-wrap: wrap; gap: 6px; margin: 8px 0 12px; }
nav a { background: var(--chip); color: var(--fg); text-decoration: none;
  padding: 1px 8px; border-radius: 10px; font-size: 12px; }
a { color: var(--accent); }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; }
th, td { border-bottom: 1px solid var(--line); padding: 4px 8px; text-align: left;
  vertical-align: top; }
th { background: var(--head); font-weight: 600; white-space: nowrap; }
.mono, td.num { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-size: 12.5px; }
td.name { font-weight: 600; overflow-wrap: anywhere; }
table.regs { table-layout: fixed; min-width: 1100px; }
table.regs th:nth-child(1) { width: 17%; }
table.regs th:nth-child(2) { width: 92px; }
table.regs th:nth-child(3) { width: 72px; }
table.regs th:nth-child(4) { width: 56px; }
table.regs th:nth-child(5) { width: 64px; }
table.regs th:nth-child(6) { width: 48px; }
table.regs th:nth-child(7) { width: 90px; }
table.regs th:nth-child(8) { width: 120px; }
table.regs th:nth-child(9) { width: 52px; }
table.regs th:nth-child(10) { width: 110px; }
td.modbus { white-space: nowrap; }
.label { font-weight: 600; }
.desc { white-space: pre-line; color: var(--muted); }
details summary { cursor: pointer; color: var(--accent); }
details table { margin-top: 4px; }
details td, details th { padding: 2px 6px; font-weight: normal; background: none; }
.hidden { display: none; }
.empty { color: var(--muted); }
"""

SCRIPT = """\
const q = document.getElementById('q'), type = document.getElementById('type');
const access = document.getElementById('access'), mb = document.getElementById('mb');
const count = document.getElementById('count');
const rows = [...document.querySelectorAll('tr.reg')];
function apply() {
  const text = q.value.trim().toLowerCase();
  let shown = 0;
  for (const r of rows) {
    const ok = (!text || r.textContent.toLowerCase().includes(text))
      && (!type.value || r.dataset.type === type.value)
      && (!access.value || r.dataset.access === access.value)
      && (!mb.checked || r.dataset.modbus === '1');
    r.classList.toggle('hidden', !ok);
    if (ok) shown++;
  }
  for (const s of document.querySelectorAll('section.block')) {
    s.classList.toggle('hidden', !s.querySelector('tr.reg:not(.hidden)'));
  }
  count.textContent = shown + ' / ' + rows.length + ' registers';
}
for (const el of [q, type, access, mb]) el.addEventListener('input', apply);
apply();
"""


def _e(value: object) -> str:
    return escape(str(value))


def _default(reg: ResolvedRegister) -> str:
    if reg.default is None:
        return ""
    if reg.type is RegType.ENUM:
        name = next((v.name for v in reg.values if v.value == reg.default), None)
        return _e(name or reg.default)
    return _e(reg.default)


def _range(reg: ResolvedRegister) -> str:
    if reg.range_min is None and reg.range_max is None:
        return ""
    low = "" if reg.range_min is None else _e(reg.range_min)
    high = "" if reg.range_max is None else _e(reg.range_max)
    return f"{low} … {high}"


def _modbus_cell(reg: ResolvedRegister) -> str:
    if reg.modbus is None:
        return ""
    first, last = reg.modbus.addresses[0], reg.modbus.addresses[-1]
    span = str(first) if first == last else f"{first}–{last}"
    return f"{reg.modbus.space} {span}"


def _items(reg: ResolvedRegister) -> str:
    if not reg.items:
        return ""
    is_enum = reg.type is RegType.ENUM
    head = "Value" if is_enum else "Bit"
    rows = ""
    for item in reg.items:
        note = f" <span class=desc>{_e(item.description)}</span>" if item.description else ""
        rows += (
            f"<tr><td class=num>{item.value}</td><td class=mono>{_e(item.name)}</td>"
            f"<td>{_e(item.label)}{note}</td></tr>"
        )
    summary = f"{len(reg.items)} {'values' if is_enum else 'bits'}"
    return (
        f"<details><summary>{summary}</summary><table>"
        f"<tr><th>{head}</th><th>Name</th><th>Label</th></tr>{rows}</table></details>"
    )


def _register_row(reg: ResolvedRegister) -> str:
    label = f"<div class=label>{_e(reg.label)}</div>" if reg.label else ""
    desc = f"<div class=desc>{_e(reg.description)}</div>" if reg.description else ""
    return (
        f'<tr class=reg id="{_e(reg.full_name)}" data-type="{reg.type.value}"'
        f' data-access="{reg.access.value}" data-modbus="{int(reg.modbus is not None)}">'
        f"<td class=name>{_e(reg.full_name)}</td>"
        f"<td class=mono>0x{reg.id:08X}</td>"
        f"<td class=mono>0x{reg.address:03X}</td>"
        f"<td>{reg.type.value}</td><td>{reg.access.value}</td>"
        f"<td class=num>{reg.size}</td>"
        f"<td class=mono>{_default(reg)}</td>"
        f"<td class=mono>{_range(reg)}</td>"
        f"<td>{_e(reg.unit)}</td>"
        f"<td class='mono modbus'>{_modbus_cell(reg)}</td>"
        f"<td>{label}{desc}{_items(reg)}</td></tr>\n"
    )


def _block_section(block: ResolvedBlock) -> str:
    head = _e(block.abbrev) + (f" — {_e(block.name)}" if block.name else "")
    note = f"<p class=sub>{_e(block.description)}</p>" if block.description else ""
    columns = ("Name", "ID", "Address", "Type", "Access", "Size", "Default", "Range", "Unit")
    cells = "".join(f"<th>{c}</th>" for c in (*columns, "Modbus", "Description"))
    rows = "".join(_register_row(r) for r in block.registers)
    return (
        f'<section class=block id="block-{_e(block.abbrev)}">'
        f"<h2>{head} <span class=sub>(code {block.code}, {block.end} B)</span></h2>{note}"
        f"<div class=scroll><table class=regs><tr>{cells}</tr>\n{rows}</table></div></section>\n"
    )


def _modbus_table(rmap: ResolvedMap, space: str, title: str) -> str:
    regs = sorted(
        (r for r in rmap.registers if r.modbus and r.modbus.space == space),
        key=lambda r: r.modbus.addresses[0],
    )
    if not regs:
        return ""
    rows = ""
    for r in regs:
        m = r.modbus
        fmt = _e(m.format) + (" × 10" if m.float_x10 else "")
        addr = ", ".join(str(a) for a in m.addresses)
        rows += (
            f'<tr><td class=mono>{addr}</td><td class=name><a href="#{_e(r.full_name)}">'
            f"{_e(r.full_name)}</a></td><td>{r.type.value}</td><td>{fmt}</td>"
            f"<td>{r.access.value}</td><td>{_e(r.unit)}</td></tr>\n"
        )
    return (
        f"<h3>{title}</h3><div class=scroll><table><tr><th>Address</th><th>Register</th>"
        f"<th>Type</th><th>Format</th><th>Access</th><th>Unit</th></tr>\n{rows}</table></div>"
    )


def _options(values: list[str]) -> str:
    return "".join(f'<option value="{v}">{v}</option>' for v in values)


def render(rmap: ResolvedMap) -> str:
    used = [b for b in rmap.blocks if b.registers]
    count = sum(len(b.registers) for b in used)
    nav = "".join(f'<a href="#block-{_e(b.abbrev)}">{_e(b.abbrev)}</a>' for b in used)
    sections = "".join(_block_section(b) for b in used)
    modbus = _modbus_table(rmap, "INPUT", "Input registers (read only)")
    modbus += _modbus_table(rmap, "HOLD", "Holding registers")
    types = _options([t.value for t in RegType])
    accesses = _options([a.value for a in Access])
    none = "<p class=empty>No register is exposed on Modbus.</p>"
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{_e(rmap.name)} register map</title>
<style>
{STYLE}</style>
</head>
<body>
<header>
<h1>{_e(rmap.name)} register map</h1>
<p class=sub>{count} registers in {len(used)} blocks.
Generated by regmap; edit the YAML map, not this file.</p>
<nav>{nav}<a href="#modbus">Modbus map</a></nav>
</header>
<div class=bar>
<input id=q type=search placeholder="Search name, label, description…" aria-label="Search">
<select id=type aria-label="Type"><option value="">all types</option>{types}</select>
<select id=access aria-label="Access"><option value="">all access</option>{accesses}</select>
<label><input id=mb type=checkbox> Modbus only</label>
<span id=count class=sub></span>
</div>
<main>
{sections}<section id=modbus><h2>Modbus map</h2>
{modbus or none}</section>
</main>
<script>
{SCRIPT}</script>
</body>
</html>
"""
