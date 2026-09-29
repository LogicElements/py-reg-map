# Templates

The C outputs are produced by filling placeholders in templates. The package ships default
templates (`src/regmap/templates/`); a project can replace any of them.

| template | output | placeholders |
|---|---|---|
| `reg_map_temp.h` | `reg_map.h` | `/* < DEFINE REG MAP > */`, `/* < DEFINE REG BITS > */`, `/* < REG MAP PARAMS > */`, `/* < REG MAP TYPEDEFS > */` |
| `reg_map_temp.c` | `reg_map.c` | `/* < DEFINE REG MAP STORAGE > */`, `/* < REG MAP FACTORY > */` |
| `mb_rtu_app_temp.h` | `mb_rtu_app.h` | `/* < MODBUS INPUT DEFINE > */`, `/* < MODBUS HOLD DEFINE > */` |
| `mb_rtu_app_temp.c` | `mb_rtu_app.c` | `/* < READ INPUT REG > */`, `/* < READ HOLD REG > */`, `/* < WRITE HOLD REG > */` |

## Project templates

Point `generator.templates` to a directory (relative to the YAML file):

```yaml
generator:
  templates: templates
```

A file there with one of the names above is used instead of the default; missing files fall
back to the defaults, so a project overrides only what it needs.

If `generator.templates` is set, the directory must exist; otherwise generation stops with an
error (a typo must not silently fall back to the defaults).

Rules for custom templates:

- Every placeholder of the template must be present exactly as written above; a missing
  placeholder stops the generation with an error.
- The generated text replaces the placeholder; the rest of the line and all other text stay
  unchanged. Each generated block ends with a newline, so the placeholder's own line end
  becomes an empty line.
- Templates may use LF or CRLF line endings and UTF-8 (with or without BOM) or cp1250; outputs
  are always UTF-8 with CRLF.
