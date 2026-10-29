from pathlib import Path

from pydantic import BaseModel, ConfigDict, FiniteFloat

from correctionlib.plugins import NodePlugin


class ScaleConfig(BaseModel):
    model_config = ConfigDict(extra="forbid")
    input: str
    factor: FiniteFloat


class OffsetConfig(BaseModel):
    model_config = ConfigDict(extra="forbid")
    amount: FiniteFloat


def _library() -> Path:
    root = Path(__file__).parent
    candidates = [
        p
        for p in root.iterdir()
        if p.suffix in (".so", ".dylib", ".dll")
        and "example_correction_nodes" in p.name
    ]
    if len(candidates) != 1:
        raise RuntimeError("Expected exactly one native example provider library")
    return candidates[0]


def scale() -> NodePlugin:
    return NodePlugin(_library(), config_model=ScaleConfig)


def offset() -> NodePlugin:
    return NodePlugin(_library(), config_model=OffsetConfig)
