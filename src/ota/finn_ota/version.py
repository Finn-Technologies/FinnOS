"""Semantic version parsing and comparison for FinnOS OTA."""

from __future__ import annotations

import re
from dataclasses import dataclass
from functools import total_ordering
from typing import Tuple, Union

_SEMVER_RE = re.compile(
    r"^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def _pre_key(ident: str) -> Tuple[int, Union[int, str]]:
    if ident.isdigit():
        return (0, int(ident))
    return (1, ident)


@total_ordering
@dataclass(frozen=True)
class SemVer:
    major: int
    minor: int
    patch: int
    prerelease: Tuple[str, ...] = ()
    raw: str = ""

    @classmethod
    def parse(cls, value: str) -> "SemVer":
        text = value.strip()
        m = _SEMVER_RE.match(text)
        if not m:
            raise ValueError(f"invalid semantic version: {value!r}")
        major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
        pre = tuple(m.group(4).split(".")) if m.group(4) else ()
        return cls(major=major, minor=minor, patch=patch, prerelease=pre, raw=text)

    def _cmp_tuple(self) -> Tuple[int, int, int, int, Tuple[Tuple[int, Union[int, str]], ...]]:
        # Absence of prerelease sorts higher than any prerelease (SemVer 2.0.0)
        is_release = 1 if not self.prerelease else 0
        pre_keys = tuple(_pre_key(p) for p in self.prerelease)
        return (self.major, self.minor, self.patch, is_release, pre_keys)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, SemVer):
            return NotImplemented
        return self._cmp_tuple() == other._cmp_tuple()

    def __lt__(self, other: object) -> bool:
        if not isinstance(other, SemVer):
            return NotImplemented
        return self._cmp_tuple() < other._cmp_tuple()

    def __str__(self) -> str:
        base = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            return f"{base}-{'.'.join(self.prerelease)}"
        return base
