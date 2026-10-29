"""Optional Python discovery for native correction nodes.

Entry points in ``correctionlib.nodes`` return a NodePlugin. The native library
remains responsible for configuration validation and all numerical execution.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from importlib.metadata import EntryPoint, distributions
from pathlib import Path
from threading import RLock
from typing import Any

from pydantic import BaseModel

from correctionlib import _core


@dataclass(frozen=True)
class NodePlugin:
    library: str | Path
    versions: tuple[int, ...] = (1,)
    config_model: type[BaseModel] | None = None


_lock = RLock()
_loaded: dict[str, NodePlugin] = {}
_enabled = True


def enable_discovery(enabled: bool = True) -> None:
    """Control automatic imports; explicitly loaded native nodes still work."""
    global _enabled
    with _lock:
        _enabled = enabled


def load_library(path: str | Path) -> None:
    """Explicitly load a native provider without entry-point discovery."""
    _core.load_plugin(str(path))


def _node_entry_points() -> list[EntryPoint]:
    # Distribution.entry_points works on every supported Python, including 3.9
    # where entry_points(group=...) is not yet part of the stdlib API.
    return [
        ep
        for distribution in distributions()
        for ep in distribution.entry_points
        if ep.group == "correctionlib.nodes"
    ]


def _provider(node_type: str, version: int) -> NodePlugin | None:
    with _lock:
        descriptor = _loaded.get(node_type)
        if descriptor is None and _enabled:
            # Enumerate metadata, but import only the exact requested provider.
            matches = [ep for ep in _node_entry_points() if ep.name == node_type]
            if len(matches) > 1:
                raise ValueError(f"Duplicate entry-point providers for {node_type}")
            if matches:
                descriptor = matches[0].load()()
                if not isinstance(descriptor, NodePlugin):
                    raise ValueError(f"Provider {node_type} must return NodePlugin")
                if version not in descriptor.versions:
                    raise ValueError(
                        f"Provider {node_type} does not support version {version}"
                    )
                load_library(descriptor.library)
                if not all(_core.has_plugin(node_type, v) for v in descriptor.versions):
                    raise ValueError(
                        f"Native provider did not register advertised type {node_type}"
                    )
                _loaded[node_type] = descriptor
        if descriptor is not None and version not in descriptor.versions:
            raise ValueError(f"Provider {node_type} does not support version {version}")
        if not _core.has_plugin(node_type, version):
            raise ValueError(f"Missing native plugin {node_type} version {version}")
        return descriptor


def validate_config(node_type: str, version: int, config: dict[str, Any]) -> None:
    descriptor = _provider(node_type, version)
    if descriptor is not None and descriptor.config_model is not None:
        # Validate without rewriting the serialized numerical prescription.
        descriptor.config_model.model_validate(config)


def prepare(data: str) -> None:
    """Register required native nodes before parsing JSON in the evaluator."""

    def visit(value: Any, path: str) -> None:
        if isinstance(value, dict):
            if value.get("nodetype") == "extension":
                try:
                    validate_config(value["type"], value["version"], value["config"])
                except (KeyError, TypeError, RuntimeError, ValueError) as exc:
                    raise ValueError(f"{path}: {exc}") from exc
                visit(value.get("children", []), path + ".children")
            else:
                for key, child in value.items():
                    # Opaque native model configurations are not correction trees.
                    if key not in ("config", "opaque"):
                        visit(child, path + "." + key)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, f"{path}[{index}]")

    visit(json.loads(data), "$")
