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
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Set, Tuple

import pandas as pd


ANCESTOR_SIGNATURE = "ancestor"
SNAPSHOT_MAGIC = b"CELXPOP1"
SNAPSHOT_VERSION = 2

_POPULATION_CSV_RE = re.compile(r"population_generation_(\d+)\.csv$")
_POPULATION_BIN_RE = re.compile(r"population_generation_(\d+)\.bin$")
_MUTATION_RE = re.compile(r"\((\d+),(\d+)\)")

_HEADER_STRUCT = struct.Struct("<8sIIdIIBBB13x")
_RECORD_STRUCT = struct.Struct("<IIffffHHIB3x")
_DRIVER_MUTATION_STRUCT = struct.Struct("<IB")


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


def parse_mutations(mutations_str: object) -> List[Tuple[int, int]]:
    if mutations_str is None or mutations_str == "" or mutations_str == '""' or pd.isna(mutations_str):
        return []
    mutations: List[Tuple[int, int]] = []
    for match in _MUTATION_RE.finditer(str(mutations_str).strip('"')):
        mutations.append((int(match.group(1)), int(match.group(2))))
    return mutations


def signature_to_driver_ids(signature: str) -> Tuple[int, ...]:
    if not signature or signature == ANCESTOR_SIGNATURE:
        return ()
    return tuple(int(part) for part in signature.split(","))


def driver_signature_from_mutations(
    mutations: Sequence[Tuple[int, int]], driver_type_ids: Set[int]
) -> str:
    driver_ids = [mutation_id for mutation_id, mutation_type in mutations if mutation_type in driver_type_ids]
    return driver_signature_from_driver_ids(driver_ids)


def driver_signature_from_driver_ids(driver_ids: Iterable[int]) -> str:
    normalized = sorted({int(driver_id) for driver_id in driver_ids})
    if not normalized:
        return ANCESTOR_SIGNATURE
    return ",".join(str(driver_id) for driver_id in normalized)


def find_parent_signature(signature: str, all_signatures: Iterable[str]) -> str:
    signature_set = set(all_signatures)
    if signature == ANCESTOR_SIGNATURE:
        return ""

    parts = list(signature_to_driver_ids(signature))
    for index in range(len(parts)):
        candidate_parts = parts[:index] + parts[index + 1 :]
        candidate = driver_signature_from_driver_ids(candidate_parts)
        if candidate in signature_set:
            return candidate

    return ANCESTOR_SIGNATURE


def signature_depth(signature: str) -> int:
    return len(signature_to_driver_ids(signature))


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
        return _load_population_bin(source)
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
        added_driver_id = driver_ids[-1] if driver_ids else None
        metadata_rows.append(
            {
                "CloneSignature": signature,
                "ParentSignature": find_parent_signature(signature, all_signatures),
                "CloneDepth": signature_depth(signature),
                "FirstGeneration": first_generation,
                "LastGeneration": last_generation,
                "PeakCells": float(series.max()),
                "TotalCells": float(series.sum()),
                "BirthTime": max((death_times.get(driver_id, 0.0) for driver_id in driver_ids), default=0.0),
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


def _load_population_bin(source: PopulationFrameSource) -> SnapshotFrame:
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

        records = [_RECORD_STRUCT.unpack(handle.read(_RECORD_STRUCT.size)) for _ in range(record_count)]
        driver_mutations = [
            _DRIVER_MUTATION_STRUCT.unpack(handle.read(_DRIVER_MUTATION_STRUCT.size))
            for _ in range(driver_mutation_count)
        ]

    rows: List[Dict] = []
    has_driver_payload = bool(flags & 0x1) and driver_mutation_count > 0

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
        driver_slice = driver_mutations[driver_offset : driver_offset + driver_count] if has_driver_payload else []
        driver_ids = [mutation_id for mutation_id, _ in driver_slice]
        mutations_str = " ".join(f"({mutation_id},{mutation_type})" for mutation_id, mutation_type in driver_slice)
        signature = driver_signature_from_driver_ids(driver_ids)
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
