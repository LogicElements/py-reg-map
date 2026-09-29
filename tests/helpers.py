"""Small builders for register maps used across the tests."""

import textwrap
from pathlib import Path

import pytest

from regmap.model import MapError, RegisterMap, load_map_text

EXAMPLE = Path(__file__).resolve().parents[1] / "example"


def map_yaml(blocks: str, device: str = "name: Dev", extra: str = "") -> str:
    """A complete map document; ``blocks`` is the YAML under ``blocks:`` (dedented)."""
    body = textwrap.indent(textwrap.dedent(blocks).strip("\n"), "  ")
    return f"device: {{{device}}}\n{textwrap.dedent(extra)}blocks:\n{body}\n"


def load(blocks: str, **kwargs: str) -> RegisterMap:
    return load_map_text(map_yaml(blocks, **kwargs))


def errors(blocks: str, **kwargs: str) -> list[str]:
    """Messages of the MapError raised while loading ``blocks``."""
    with pytest.raises(MapError) as exc:
        load(blocks, **kwargs)
    return exc.value.errors
