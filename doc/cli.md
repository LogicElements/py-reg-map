# Command line

```
regmap init NAME [-o MAP.yaml] [--force]
regmap generate MAP.yaml [--out DIR] [--allow-id-change] [--check]
regmap import-xlsx WORKBOOK.xlsm [-o MAP.yaml] [--force]
regmap schema [-o FILE]
regmap --version
```

## `init`

Creates a new register map for a device called `NAME` (a C identifier; it becomes
`device.name` and the output file names). The default file is `<name lowercase>.yaml` in the
current directory; an existing file is only overwritten with `--force`.

The map is a starter map with the registers every device needs, a `generator` section listing
all parameters (every output next to the YAML file) and the format reference in the header.
The first line points editors to `regmap.schema.json`, which `init` writes next to the map
(and refreshes if it already exists), so completion and validation work straight away (see
[yaml-format.md](yaml-format.md#editor-support)). Edit the map, then run `regmap generate`.

## `generate`

1. Loads and validates the map; all problems are printed at once.
2. Renders all seven outputs in memory (templates from `generator.templates`, otherwise the
   package defaults — see [templates.md](templates.md)).
3. Destinations: `generator.outputs.<key>` relative to the YAML file, otherwise the YAML
   file's directory. `--out DIR` writes everything to `DIR` instead.
4. Runs the [stability check](stability-check.md) against the previous JSON outputs.
5. Writes only files whose content changed and prints `written` or `unchanged` per file
   (unchanged files keep their timestamps, so the firmware is not rebuilt needlessly).

Options:

| option | effect |
|---|---|
| `--out DIR` | all outputs into `DIR`, `generator.outputs` ignored |
| `--allow-id-change` | accept breaking ID / Modbus changes (printed as a warning) |
| `--check` | write nothing; print `outdated <file>` for every missing or different output and exit 1 if there is any — for CI pipelines that verify the committed outputs match the map |

## `import-xlsx`

Converts a legacy Excel workbook into a YAML map, see [import-xlsx.md](import-xlsx.md).
`-o` defaults to `<workbook name in lower case>.yaml` next to the workbook; an existing file
is overwritten only with `--force`. Requires the `xlsx` extra.

## `schema`

Prints the JSON Schema of the YAML format, or writes it to `FILE` (see
[yaml-format.md](yaml-format.md#editor-support)).

## Exit codes

| code | meaning |
|---|---|
| 0 | success |
| 1 | invalid map, template error, breaking change, `--check` found differences, import failed |
| 2 | bad arguments or input file not found |
