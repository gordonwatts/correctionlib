"""Experimental fork schema: fixed numeric array outputs; v2 stays unchanged.

Reuse v2 field validation for the same node syntax, while replacing recursive
content fields so every selector can hold an explicit Array node.
The targeted assignment ignores below describe intentional schema-version
widening in Pydantic models, rather than mutable evaluator inheritance. Native loading
checks the complete graph's types and shapes, including external providers.
"""

from collections import Counter
from functools import partial
from typing import Annotated, Literal, Optional, Union

from pydantic import Field, model_validator

from correctionlib import schemav2 as v2

VERSION = 3
Dimension = Annotated[int, Field(strict=True, gt=0)]
Shape = Annotated[list[Dimension], Field(min_length=1, max_length=1)]


class Output(v2.Variable):
    type: Literal["real"]
    shape: Shape = Field(default=None)  # type: ignore[arg-type]

    def __rich__(self) -> str:
        return super().__rich__() + (f"\nShape: {self.shape}" if self.shape else "")


Content = Union[
    Annotated[
        Union[
            "Array",
            "Binning",
            "MultiBinning",
            "Category",
            "Transform",
            "Extension",
            v2.Formula,
            v2.FormulaRef,
            v2.HashPRNG,
            v2.LWTNN,
        ],
        Field(discriminator="nodetype"),
    ],
    float,
]


class Array(v2.Model):
    """Explicit ordered components; each component must itself be scalar."""

    nodetype: Literal["array"]
    content: list[Content] = Field(min_length=1)


class Binning(v2.Binning):
    content: list[Content]  # type: ignore[assignment]
    flow: Union[Content, Literal["clamp", "error", "wrap"]]  # type: ignore[assignment]


class MultiBinning(v2.MultiBinning):
    content: list[Content]  # type: ignore[assignment]
    flow: Union[Content, Literal["clamp", "error", "wrap"]]  # type: ignore[assignment]


class CategoryItem(v2.CategoryItem):
    value: Content  # type: ignore[assignment]


class Category(v2.Category):
    content: list[CategoryItem]  # type: ignore[assignment]
    default: Optional[Content] = None  # type: ignore[assignment]


class Transform(v2.Transform):
    rule: Content  # type: ignore[assignment]
    content: Content  # type: ignore[assignment]


class Extension(v2.Extension):
    children: list[Content] = Field(default_factory=list)  # type: ignore[assignment]
    output_shape: Shape = Field(default=None)  # type: ignore[arg-type]


def walk_content(node, func):
    func(node)
    if isinstance(node, Array):
        for child in node.content:
            walk_content(child, func)
    elif isinstance(node, Extension):
        for child in node.children:
            walk_content(child, func)
    elif isinstance(node, (Binning, MultiBinning)):
        for child in node.content:
            walk_content(child, func)
        if not isinstance(node.flow, str):
            walk_content(node.flow, func)
    elif isinstance(node, Category):
        for child in node.content:
            walk_content(child.value, func)
        if node.default is not None:
            walk_content(node.default, func)
    elif isinstance(node, Transform):
        walk_content(node.rule, func)
        walk_content(node.content, func)


class Correction(v2.Correction):
    output: Output
    data: Content  # type: ignore[assignment]

    @model_validator(mode="after")
    def check_input_names(self) -> "Correction":
        from correctionlib import _core

        walk_content(
            self.data, partial(v2._validate_input, {v.name for v in self.inputs})
        )
        try:
            _core.CorrectionSet.from_string(
                '{"schema_version":3,"corrections":['
                + self.model_dump_json(exclude_unset=True)
                + "]}"
            )
        except (RuntimeError, ValueError) as exc:
            raise ValueError(str(exc)) from exc
        return self

    def summary(self):
        nodecount = Counter()
        inputstats = {var.name: v2._SummaryInfo() for var in self.inputs}
        walk_content(self.data, partial(v2._summarize, nodecount, inputstats))
        return nodecount, inputstats

    def to_evaluator(self):
        return CorrectionSet(schema_version=3, corrections=[self]).to_evaluator()[
            self.name
        ]


class CorrectionSet(v2.CorrectionSet):
    schema_version: Literal[3]  # type: ignore[assignment]
    corrections: list[Correction]  # type: ignore[assignment]
    schema_url: Optional[str] = Field(default=None, alias="$schema")

    @model_validator(mode="after")
    def check_native_graph(self) -> "CorrectionSet":
        from correctionlib import _core

        try:
            _core.CorrectionSet.from_string(self.model_dump_json(exclude_unset=True))
        except (RuntimeError, ValueError) as exc:
            raise ValueError(str(exc)) from exc
        return self


for model in (
    Array,
    Binning,
    MultiBinning,
    CategoryItem,
    Category,
    Transform,
    Extension,
    Correction,
    CorrectionSet,
):
    model.model_rebuild()
