#!/usr/bin/env python3
from __future__ import annotations

import colorsys
import hashlib
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Set, Tuple

import pandas as pd


ANCESTOR_SIGNATURE = "ancestor"
SNAPSHOT_MAGIC = b"CELXPOP1"
SNAPSHOT_VERSION = 4

_POPULATION_CSV_RE = re.compile(r"population_generation_(\d+)\.csv$")
_POPULATION_BIN_RE = re.compile(r"population_generation_(\d+)\.bin$")
# v3 adds an optional realized effect, v4 an optional event-list slot; the
# groups stay optional so older two-field exports remain parseable. The effect
# group is a full float pattern rather than a character class so a malformed
# entry such as "(1,2,-)" is skipped instead of matching and failing
# `float(...)`.
_MUTATION_RE = re.compile(
    r"\((\d+),(\d+)(?:,([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?))?(?:,(\d+))?\)"
)

_HEADER_STRUCT = struct.Struct("<8sIIdIIBBB13x")
_RECORD_STRUCT = struct.Struct("<IIffffIIIB3x")
_DRIVER_MUTATION_STRUCT = struct.Struct("<IBHf")


def format_effect(effect: float) -> str:
    """Match the C++ CSV writer (``std::setprecision(max_digits10)``)."""
    return format(effect, ".9g")


@dataclass(frozen=True)
class PopulationFrameSource:
    generation: int
    path: Path
    kind: str


@dataclass(frozen=True)
class SnapshotFrame:
    generation: int
    tau: float
    spatial_dimensions: int
    data: pd.DataFrame


def resolve_run_dir(input_dir: str | Path) -> Path:
    return Path(input_dir).expanduser().resolve()


def load_config(run_dir: str | Path) -> Dict:
    run_path = resolve_run_dir(run_dir)
    candidate_paths = [run_path / "config.json", run_path.parent / "config.json"]
    for config_path in candidate_paths:
        if config_path.exists():
            with config_path.open("r", encoding="utf-8") as handle:
                return json.load(handle)
    raise FileNotFoundError(f"config.json not found for run directory: {run_path}")


def load_driver_type_ids(run_dir: str | Path) -> Set[int]:
    config = load_config(run_dir)
    driver_ids: Set[int] = set()
    for mutation in config.get("mutations", []):
        if mutation.get("is_driver", False):
            driver_ids.add(int(mutation.get("id")))
    return driver_ids


def load_statistics(run_dir: str | Path) -> Optional[pd.DataFrame]:
    run_path = resolve_run_dir(run_dir)
    stats_path = run_path / "statistics" / "generational_statistics.csv"
    if not stats_path.exists():
        return None
    stats = pd.read_csv(stats_path)
    if "Generation" in stats.columns:
        stats = stats.sort_values("Generation").reset_index(drop=True)
    return stats


def load_phylogeny(run_dir: str | Path) -> Optional[pd.DataFrame]:
    run_path = resolve_run_dir(run_dir)
    phylogeny_path = run_path / "phylogeny" / "phylogenetic_tree.csv"
    if not phylogeny_path.exists():
        return None
    phylogeny = pd.read_csv(phylogeny_path)
    if "NodeID" in phylogeny.columns:
        phylogeny = phylogeny.sort_values("NodeID").reset_index(drop=True)
    return phylogeny


def phylogeny_death_times(phylogeny: Optional[pd.DataFrame]) -> Dict[int, float]:
    if phylogeny is None or phylogeny.empty:
        return {}
    node_ids = phylogeny.get("NodeID")
    death_times = phylogeny.get("DeathTime")
    if node_ids is None or death_times is None:
        return {}
    return {
        int(node_id): float(death_time)
        for node_id, death_time in zip(node_ids.tolist(), death_times.tolist())
    }


def parse_mutations(mutations_str: object) -> List[Tuple[int, int, float, int]]:
    """Parse ``Mutations`` entries into ``(mutation_id, mutation_type, effect, slot)``.

    The effect is absent from pre-v3 payloads and the slot from pre-v4 payloads;
    both default to 0 in that case.
    """
    if mutations_str is None or mutations_str == "" or mutations_str == '""' or pd.isna(mutations_str):
        return []
    mutations: List[Tuple[int, int, float, int]] = []
    for match in _MUTATION_RE.finditer(str(mutations_str).strip('"')):
        effect = float(match.group(3)) if match.group(3) is not None else 0.0
        slot = int(match.group(4)) if match.group(4) is not None else 0
        mutations.append((int(match.group(1)), int(match.group(2)), effect, slot))
    return mutations


def _render_signature(alleles: Iterable[Tuple[int, Optional[int]]]) -> str:
    """Render ``(mutation_id, slot)`` allele tokens with the ADR-0013 rule.

    A token is the bare ``mutation_id`` unless that id appears more than once in
    this set, in which case every token for that id becomes ``mutation_id:slot``.
    Bernoulli-only runs carry a single slot (0) per id, so their labels are
    unchanged.
    """
    unique = sorted(
        {(int(mutation_id), None if slot is None else int(slot))
         for mutation_id, slot in alleles},
        key=lambda allele: (allele[0], -1 if allele[1] is None else allele[1]),
    )
    if not unique:
        return ANCESTOR_SIGNATURE

    slot_counts: Dict[int, int] = {}
    for mutation_id, _slot in unique:
        slot_counts[mutation_id] = slot_counts.get(mutation_id, 0) + 1

    tokens: List[str] = []
    for mutation_id, slot in unique:
        if slot is not None and slot_counts[mutation_id] > 1:
            tokens.append(f"{mutation_id}:{slot}")
        else:
            tokens.append(str(mutation_id))
    return ",".join(tokens)


def signature_to_alleles(signature: str) -> Tuple[Tuple[int, Optional[int]], ...]:
    """Parse a clone signature back into ``(mutation_id, slot)`` tokens.

    Bare tokens carry ``None`` as the slot (legacy signatures).
    """
    if not signature or signature == ANCESTOR_SIGNATURE:
        return ()
    alleles: List[Tuple[int, Optional[int]]] = []
    for part in signature.split(","):
        mutation_id, _, slot = part.partition(":")
        try:
            parsed_id = int(mutation_id)
            parsed_slot = int(slot) if slot else None
        except ValueError:
            # Skip a malformed token (e.g. "1:abc") instead of aborting the
            # whole signature; callers only read signatures we rendered.
            continue
        alleles.append((parsed_id, parsed_slot))
    return tuple(alleles)


def signature_to_driver_ids(signature: str) -> Tuple[int, ...]:
    if not signature or signature == ANCESTOR_SIGNATURE:
        return ()
    return tuple(sorted({mutation_id for mutation_id, _ in signature_to_alleles(signature)}))


def driver_signature_from_mutations(
    mutations: Iterable[Tuple[int, int, float, int]], driver_type_ids: Set[int]
) -> str:
    alleles = [
        (mutation_id, slot)
        for mutation_id, mutation_type, _effect, slot in mutations
        if mutation_type in driver_type_ids
    ]
    return _render_signature(alleles)


def driver_signature_from_driver_ids(driver_ids: Iterable[int]) -> str:
    return _render_signature((int(driver_id), None) for driver_id in driver_ids)


def find_parent_signature(signature: str, all_signatures: Iterable[str]) -> str:
    signature_set = set(all_signatures)
    if signature == ANCESTOR_SIGNATURE:
        return ""

    alleles = list(signature_to_alleles(signature))
    # All alleles acquired in one division share the origin ``mutation_id``
    # (ADR-0013), so a parent drops a whole id, not a single ``id:slot`` token.
    # Group by id rather than by neighbours so the result never depends on the
    # token order inside the signature.
    groups: Dict[int, List[Tuple[int, Optional[int]]]] = {}
    for allele in alleles:
        groups.setdefault(allele[0], []).append(allele)

    group_values = list(groups.values())
    # Mutation ids are monotonic, and rendered signatures sort ascending, so the
    # final group holds the newest event. Drop the newest group first, then fall
    # back to left-to-right. This matches ``ResultsParser._muller_parent_signature``
    # in the web backend so both trees agree when several candidate parents exist.
    drop_order = [len(group_values) - 1] + list(range(len(group_values) - 1))
    for group_index in drop_order:
        candidate_alleles = [
            allele
            for index, group in enumerate(group_values)
            if index != group_index
            for allele in group
        ]
        candidate = _render_signature(candidate_alleles)
        if candidate in signature_set:
            return candidate

    return ANCESTOR_SIGNATURE


def signature_depth(signature: str) -> int:
    return len(signature_to_alleles(signature))


def clone_label(signature: str) -> str:
    if signature == ANCESTOR_SIGNATURE:
        return "ancestor"
    return f"clone {signature}"


def clone_rgb(signature: str, alpha: float = 1.0) -> Tuple[float, float, float, float]:
    if signature == ANCESTOR_SIGNATURE:
        return (0.62, 0.62, 0.62, alpha)

    digest = hashlib.sha1(signature.encode("utf-8")).digest()
    hue = int.from_bytes(digest[:2], "big") / 65535.0
    saturation = 0.65 + (digest[2] / 255.0) * 0.20
    value = 0.82 + (digest[3] / 255.0) * 0.12
    red, green, blue = colorsys.hsv_to_rgb(hue, saturation, value)
    return (red, green, blue, alpha)


def clone_hex(signature: str) -> str:
    red, green, blue, _ = clone_rgb(signature)
    return "#{:02x}{:02x}{:02x}".format(
        int(red * 255),
        int(green * 255),
        int(blue * 255),
    )


def annotate_clone_columns(df: pd.DataFrame, driver_type_ids: Set[int]) -> pd.DataFrame:
    working = df.copy()

    signatures: List[str] = []
    for mutation_entry in working.get("Mutations", pd.Series([""] * len(working))):
        signatures.append(driver_signature_from_mutations(parse_mutations(mutation_entry), driver_type_ids))

    working["CloneSignature"] = signatures
    working["CloneLabel"] = [clone_label(signature) for signature in signatures]
    working["IsAncestor"] = [signature == ANCESTOR_SIGNATURE for signature in signatures]
    working["CloneColorHex"] = [clone_hex(signature) for signature in signatures]
    return working


def population_data_dir(run_dir: str | Path) -> Path:
    run_path = resolve_run_dir(run_dir)
    candidate = run_path / "population_data"
    return candidate if candidate.exists() else run_path


def discover_population_sources(
    run_dir: str | Path,
    prefer_bin: bool = False,
) -> List[PopulationFrameSource]:
    data_dir = population_data_dir(run_dir)
    csv_sources: List[PopulationFrameSource] = []
    bin_sources: List[PopulationFrameSource] = []

    for path in sorted(data_dir.iterdir()):
        if not path.is_file():
            continue

        csv_match = _POPULATION_CSV_RE.match(path.name)
        if csv_match:
            csv_sources.append(
                PopulationFrameSource(generation=int(csv_match.group(1)), path=path, kind="csv")
            )
            continue

        bin_match = _POPULATION_BIN_RE.match(path.name)
        if bin_match:
            bin_sources.append(
                PopulationFrameSource(generation=int(bin_match.group(1)), path=path, kind="bin")
            )

    csv_sources.sort(key=lambda item: item.generation)
    bin_sources.sort(key=lambda item: item.generation)

    if prefer_bin and bin_sources:
        return bin_sources
    if csv_sources:
        return csv_sources
    if bin_sources:
        return bin_sources
    return []


def load_population_frame(
    source: PopulationFrameSource,
    driver_type_ids: Set[int],
) -> SnapshotFrame:
    if source.kind == "csv":
        return _load_population_csv(source, driver_type_ids)
    if source.kind == "bin":
        return _load_population_bin(source, driver_type_ids)
    raise ValueError(f"Unsupported population source kind: {source.kind}")


def iter_population_frames(
    run_dir: str | Path,
    driver_type_ids: Optional[Set[int]] = None,
    prefer_bin: bool = False,
) -> Iterator[SnapshotFrame]:
    resolved_driver_ids = driver_type_ids if driver_type_ids is not None else load_driver_type_ids(run_dir)
    for source in discover_population_sources(run_dir, prefer_bin=prefer_bin):
        yield load_population_frame(source, resolved_driver_ids)


def load_population_frames(
    run_dir: str | Path,
    driver_type_ids: Optional[Set[int]] = None,
    prefer_bin: bool = False,
) -> List[SnapshotFrame]:
    return list(iter_population_frames(run_dir, driver_type_ids=driver_type_ids, prefer_bin=prefer_bin))


def build_clone_timecourse(
    run_dir: str | Path,
    prefer_bin: bool = False,
) -> Tuple[pd.DataFrame, pd.DataFrame, Optional[pd.DataFrame], Optional[pd.DataFrame]]:
    driver_type_ids = load_driver_type_ids(run_dir)
    counts_rows: List[pd.Series] = []
    generations: List[int] = []

    for frame in iter_population_frames(run_dir, driver_type_ids=driver_type_ids, prefer_bin=prefer_bin):
        counts = frame.data["CloneSignature"].value_counts().sort_index()
        counts_rows.append(counts)
        generations.append(frame.generation)

    if not counts_rows:
        raise ValueError(f"No population snapshots found for run: {resolve_run_dir(run_dir)}")

    counts_df = pd.DataFrame(counts_rows, index=generations).fillna(0.0).sort_index()
    counts_df.index.name = "Generation"
    counts_df = counts_df.astype(float)

    phylogeny = load_phylogeny(run_dir)
    stats = load_statistics(run_dir)
    metadata = build_clone_metadata(counts_df, phylogeny)
    return counts_df, metadata, phylogeny, stats


def build_clone_metadata(
    counts_df: pd.DataFrame,
    phylogeny: Optional[pd.DataFrame] = None,
) -> pd.DataFrame:
    death_times = phylogeny_death_times(phylogeny)
    all_signatures = set(str(column) for column in counts_df.columns)
    metadata_rows: List[Dict] = []

    for signature in all_signatures:
        series = counts_df[signature]
        active_generations = series[series > 0]
        if active_generations.empty:
            first_generation = math.nan
            last_generation = math.nan
        else:
            first_generation = float(active_generations.index.min())
            last_generation = float(active_generations.index.max())

        driver_ids = signature_to_driver_ids(signature)
        parent_signature = find_parent_signature(signature, all_signatures)
        # Drivers this clone acquired relative to its parent, not merely the
        # highest id it carries (ADR-0013).
        added_driver_ids = sorted(
            set(driver_ids) - set(signature_to_driver_ids(parent_signature))
        )
        added_driver_id = added_driver_ids[0] if added_driver_ids else None
        metadata_rows.append(
            {
                "CloneSignature": signature,
                "ParentSignature": parent_signature,
                "CloneDepth": signature_depth(signature),
                "FirstGeneration": first_generation,
                "LastGeneration": last_generation,
                "PeakCells": float(series.max()),
                "TotalCells": float(series.sum()),
                "BirthTime": max(
                    (death_times.get(driver_id, 0.0) for driver_id in added_driver_ids),
                    default=0.0,
                ),
                "AddedDriverId": added_driver_id,
                "CloneLabel": clone_label(signature),
                "CloneColorHex": clone_hex(signature),
                "IsAncestor": signature == ANCESTOR_SIGNATURE,
            }
        )

    metadata = pd.DataFrame(metadata_rows)
    return metadata.sort_values(
        by=["IsAncestor", "CloneDepth", "PeakCells", "CloneSignature"],
        ascending=[False, True, False, True],
    ).reset_index(drop=True)


def pick_top_clones(
    counts_df: pd.DataFrame,
    metadata_df: pd.DataFrame,
    max_clones: int,
) -> Tuple[pd.DataFrame, pd.DataFrame]:
    if max_clones <= 0 or len(counts_df.columns) <= max_clones:
        return counts_df.copy(), metadata_df.copy()

    metadata = metadata_df.copy()
    keep_signatures = metadata.sort_values(
        by=["IsAncestor", "PeakCells", "TotalCells"],
        ascending=[False, False, False],
    )["CloneSignature"].head(max_clones).tolist()

    for signature in list(keep_signatures):
        current = signature
        while current and current != ANCESTOR_SIGNATURE:
            parent = metadata.loc[metadata["CloneSignature"] == current, "ParentSignature"]
            if parent.empty:
                break
            current = str(parent.iloc[0])
            if current and current not in keep_signatures:
                keep_signatures.append(current)

    keep_signatures = sorted(set(keep_signatures), key=lambda item: (signature_depth(item), item))
    reduced = counts_df.reindex(columns=keep_signatures, fill_value=0.0).copy()
    hidden_columns = [column for column in counts_df.columns if column not in keep_signatures]
    if hidden_columns:
        reduced["other"] = counts_df[hidden_columns].sum(axis=1)

    reduced_metadata = metadata[metadata["CloneSignature"].isin(keep_signatures)].copy()
    if hidden_columns:
        other_row = {
            "CloneSignature": "other",
            "ParentSignature": ANCESTOR_SIGNATURE,
            "CloneDepth": 1,
            "FirstGeneration": float(reduced.index.min()),
            "LastGeneration": float(reduced.index.max()),
            "PeakCells": float(reduced["other"].max()),
            "TotalCells": float(reduced["other"].sum()),
            "BirthTime": 0.0,
            "AddedDriverId": None,
            "CloneLabel": "other clones",
            "CloneColorHex": "#3f4650",
            "IsAncestor": False,
        }
        reduced_metadata.loc[len(reduced_metadata)] = other_row

    return reduced, reduced_metadata.reset_index(drop=True)


def _load_population_csv(source: PopulationFrameSource, driver_type_ids: Set[int]) -> SnapshotFrame:
    df = pd.read_csv(source.path)
    if "PositionValid" not in df.columns:
        df["PositionValid"] = 0
    if "SpatialDimensions" not in df.columns:
        spatial_dimensions = 3 if {"X", "Y", "Z"}.issubset(df.columns) else 0
        df["SpatialDimensions"] = spatial_dimensions

    annotated = annotate_clone_columns(df, driver_type_ids)
    spatial_dimensions = int(annotated["SpatialDimensions"].iloc[0]) if not annotated.empty else 0
    return SnapshotFrame(
        generation=source.generation,
        tau=float(source.generation),
        spatial_dimensions=spatial_dimensions,
        data=annotated,
    )


def _load_population_bin(source: PopulationFrameSource, driver_type_ids: Set[int]) -> SnapshotFrame:
    with source.path.open("rb") as handle:
        header_bytes = handle.read(_HEADER_STRUCT.size)
        if len(header_bytes) != _HEADER_STRUCT.size:
            raise ValueError(f"Incomplete snapshot header: {source.path}")

        magic, version, record_size, tau, record_count, driver_mutation_count, spatial_dimensions, mutation_record_size, flags = _HEADER_STRUCT.unpack(
            header_bytes
        )

        if magic != SNAPSHOT_MAGIC:
            raise ValueError(f"Invalid snapshot magic in {source.path}")
        if version != SNAPSHOT_VERSION:
            raise ValueError(f"Unsupported snapshot version {version} in {source.path}")
        if record_size != _RECORD_STRUCT.size:
            raise ValueError(f"Unexpected record size {record_size} in {source.path}")
        if mutation_record_size != _DRIVER_MUTATION_STRUCT.size:
            raise ValueError(f"Unexpected mutation record size {mutation_record_size} in {source.path}")

        expected_size = (
            _HEADER_STRUCT.size
            + record_count * record_size
            + driver_mutation_count * mutation_record_size
        )
        actual_size = source.path.stat().st_size
        if actual_size != expected_size:
            raise ValueError(
                f"Truncated or oversized snapshot {source.path}: "
                f"expected {expected_size} bytes, found {actual_size}"
            )

        records = [_RECORD_STRUCT.unpack(handle.read(_RECORD_STRUCT.size)) for _ in range(record_count)]
        driver_mutations = [
            _DRIVER_MUTATION_STRUCT.unpack(handle.read(_DRIVER_MUTATION_STRUCT.size))
            for _ in range(driver_mutation_count)
        ]

    rows: List[Dict] = []
    has_driver_payload = bool(flags & 0x1) and driver_mutation_count > 0
    has_full_payload = bool(flags & 0x2) and driver_mutation_count > 0
    has_payload = has_driver_payload or has_full_payload

    for (
        cell_id,
        parent_id,
        fitness,
        x_coord,
        y_coord,
        z_coord,
        mutation_count,
        driver_count,
        driver_offset,
        position_valid,
    ) in records:
        # The packed entry is ``(id, type, slot, effect)``; normalize it to the
        # ``(id, type, effect, slot)`` order that ``driver_signature_from_mutations``
        # and ``parse_mutations`` share before it reaches the signature helpers.
        mutation_slice = (
            [
                (mutation_id, mutation_type, effect, slot)
                for mutation_id, mutation_type, slot, effect in driver_mutations[
                    driver_offset : driver_offset + driver_count
                ]
            ]
            if has_payload
            else []
        )
        mutations_str = " ".join(
            f"({mutation_id},{mutation_type},{format_effect(effect)},{slot})"
            for mutation_id, mutation_type, effect, slot in mutation_slice
        )
        if has_full_payload:
            signature = driver_signature_from_mutations(mutation_slice, driver_type_ids)
        else:
            signature = _render_signature(
                (mutation_id, slot)
                for mutation_id, mutation_type, _effect, slot in mutation_slice
            )
        rows.append(
            {
                "CellID": cell_id,
                "ParentID": parent_id,
                "Fitness": fitness,
                "MutationCount": mutation_count,
                "Mutations": mutations_str,
                "X": x_coord if position_valid else math.nan,
                "Y": y_coord if position_valid else math.nan,
                "Z": z_coord if position_valid else math.nan,
                "PositionValid": int(position_valid),
                "SpatialDimensions": spatial_dimensions,
                "CloneSignature": signature,
                "CloneLabel": clone_label(signature),
                "IsAncestor": signature == ANCESTOR_SIGNATURE,
                "CloneColorHex": clone_hex(signature),
            }
        )

    frame_df = pd.DataFrame(rows)
    return SnapshotFrame(
        generation=source.generation,
        tau=float(tau),
        spatial_dimensions=int(spatial_dimensions),
        data=frame_df,
    )
