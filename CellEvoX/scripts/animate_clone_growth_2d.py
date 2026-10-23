#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib

matplotlib.use("Agg")

import matplotlib.animation as animation
import matplotlib.gridspec as gridspec
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from snapshot_io import ANCESTOR_SIGNATURE, build_clone_timecourse, pick_top_clones, signature_depth


BACKGROUND = "#0f1115"
PANEL = "#171a21"
GRID = "#2b313c"
TEXT = "#f2f4f8"
SECONDARY_TEXT = "#b3bac5"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Animate clone growth in 2D from CellEvoX run data.")
    parser.add_argument("--input", required=True, help="Path to a simulation output directory")
    parser.add_argument(
        "--output",
        default=None,
        help="Output animation path (default: <input>/visualizations/clone_growth_2d.mp4)",
    )
    parser.add_argument("--fps", type=int, default=24, help="Animation frame rate")
    parser.add_argument("--dpi", type=int, default=180, help="Output DPI")
    parser.add_argument(
        "--max-clones",
        type=int,
        default=14,
        help="Maximum number of major clone bands to show before aggregating the tail",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=220,
        help="Resample long time series to at most this many animation frames",
    )
    parser.add_argument(
        "--preview-frame",
        type=float,
        default=None,
        help="If set, save a single PNG at this generation fraction (0-1) or absolute generation",
    )
    return parser.parse_args()


def default_output_path(run_dir: Path) -> Path:
    return run_dir / "visualizations" / "clone_growth_2d.mp4"


def prepare_writer(output_path: Path, fps: int) -> Tuple[animation.AbstractMovieWriter, Path]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    suffix = output_path.suffix.lower()
    if suffix == ".gif":
        return animation.PillowWriter(fps=fps), output_path

    if animation.writers.is_available("ffmpeg"):
        return animation.FFMpegWriter(
            fps=fps,
            metadata={"title": "CellEvoX clone growth 2D"},
            codec="libx264",
            bitrate=2400,
        ), output_path

    fallback = output_path.with_suffix(".gif")
    print(f"Warning: ffmpeg not available, saving GIF instead: {fallback}")
    return animation.PillowWriter(fps=max(1, min(fps, 18))), fallback


def resample_counts(counts_df: pd.DataFrame, max_frames: int) -> Tuple[np.ndarray, np.ndarray]:
    generations = counts_df.index.to_numpy(dtype=float)
    if len(generations) <= max_frames:
        return generations, counts_df.to_numpy(dtype=float).T

    sampled_generations = np.linspace(generations.min(), generations.max(), num=max_frames)
    sampled_matrix = []
    for column in counts_df.columns:
        sampled_matrix.append(np.interp(sampled_generations, generations, counts_df[column].to_numpy(dtype=float)))
    return sampled_generations, np.asarray(sampled_matrix)


def build_tree_layout(metadata_df: pd.DataFrame) -> Tuple[Dict[str, Tuple[float, float]], List[Tuple[str, str]]]:
    metadata = metadata_df.set_index("CloneSignature")
    visible = [signature for signature in metadata.index if signature != "other"]
    children: Dict[str, List[str]] = {signature: [] for signature in visible}
    edges: List[Tuple[str, str]] = []

    for signature in visible:
        if signature == ANCESTOR_SIGNATURE:
            continue
        parent = str(metadata.loc[signature, "ParentSignature"])
        if parent not in children:
            parent = ANCESTOR_SIGNATURE
        children[parent].append(signature)
        edges.append((parent, signature))

    for signature, clone_children in children.items():
        clone_children.sort(
            key=lambda child: (
                metadata.loc[child, "FirstGeneration"],
                -metadata.loc[child, "PeakCells"],
                child,
            )
        )

    positions: Dict[str, Tuple[float, float]] = {}
    leaf_order: List[str] = []

    def assign_order(node: str) -> None:
        if not children.get(node):
            leaf_order.append(node)
            return
        for child in children[node]:
            assign_order(child)

    assign_order(ANCESTOR_SIGNATURE)
    y_lookup = {leaf: index for index, leaf in enumerate(leaf_order or [ANCESTOR_SIGNATURE])}

    def assign_positions(node: str) -> float:
        if not children.get(node):
            y_coord = float(y_lookup.get(node, 0))
            positions[node] = (float(signature_depth(node)), y_coord)
            return y_coord

        child_y = [assign_positions(child) for child in children[node]]
        y_coord = float(sum(child_y) / len(child_y))
        positions[node] = (float(signature_depth(node)), y_coord)
        return y_coord

    assign_positions(ANCESTOR_SIGNATURE)
    return positions, edges


def signature_display_text(signature: str) -> str:
    if signature == ANCESTOR_SIGNATURE:
        return "WT"
    if signature == "other":
        return "other"
    parts = [part for part in signature.split(",") if part]
    if not parts:
        return "?"
    # Last acquired driver id, truncated for readability.
    last = parts[-1]
    if len(last) > 6:
        last = f"…{last[-5:]}"
    return f"+{last}"


def assign_short_clone_ids(metadata_df: pd.DataFrame) -> Dict[str, str]:
    """Map long driver signatures to short stable labels (WT / C1 / C2 / …)."""
    mapping: Dict[str, str] = {}
    ordered = metadata_df.sort_values(
        by=["IsAncestor", "FirstGeneration", "PeakCells", "CloneSignature"],
        ascending=[False, True, False, True],
    )
    counter = 1
    for _, row in ordered.iterrows():
        signature = str(row["CloneSignature"])
        if signature == ANCESTOR_SIGNATURE:
            mapping[signature] = "WT"
        elif signature == "other":
            mapping[signature] = "other"
        else:
            mapping[signature] = f"C{counter}"
            counter += 1
    return mapping


def short_clone_caption(signature: str, short_ids: Dict[str, str], *, with_driver_hint: bool = False) -> str:
    short = short_ids.get(signature, "?")
    if not with_driver_hint or signature in (ANCESTOR_SIGNATURE, "other"):
        return short
    return f"{short} ({signature_display_text(signature)})"


def draw_tree_panel(
    ax: plt.Axes,
    metadata_df: pd.DataFrame,
    positions: Dict[str, Tuple[float, float]],
    edges: List[Tuple[str, str]],
    current_generation: float,
    current_counts: Dict[str, float],
    short_ids: Dict[str, str],
) -> None:
    ax.clear()
    ax.set_facecolor(PANEL)
    ax.set_title("Clone tree", color=TEXT, fontsize=15, loc="left", pad=8)
    ax.axis("off")

    metadata = metadata_df.set_index("CloneSignature")
    max_current = max(current_counts.values()) if current_counts else 1.0

    for parent, child in edges:
        x_parent, y_parent = positions[parent]
        x_child, y_child = positions[child]
        child_birth = float(metadata.loc[child, "FirstGeneration"])
        visible_alpha = 0.82 if child_birth <= current_generation else 0.10
        ax.plot(
            [x_parent, x_child],
            [y_parent, y_child],
            color=metadata.loc[child, "CloneColorHex"],
            alpha=visible_alpha,
            linewidth=2.4 if child_birth <= current_generation else 1.1,
        )

    for signature, (x_coord, y_coord) in positions.items():
        row = metadata.loc[signature]
        current_value = current_counts.get(signature, 0.0)
        emerged = float(row["FirstGeneration"]) <= current_generation if not math.isnan(row["FirstGeneration"]) else False
        alpha = 0.98 if emerged else 0.14
        size = 90.0
        if max_current > 0.0 and current_value > 0.0:
            size += 700.0 * math.sqrt(current_value / max_current)
        ax.scatter(
            [x_coord],
            [y_coord],
            s=size,
            color=row["CloneColorHex"],
            alpha=alpha,
            edgecolors="#0b0d11",
            linewidths=1.0,
            zorder=3,
        )
        # Label only currently living clones (plus WT). Historical nodes stay as colored dots.
        is_active = current_value > 0.5
        if (is_active or signature == ANCESTOR_SIGNATURE) and emerged:
            # Short ID sits a bit above the node; lift scales with marker size.
            radius_pts = math.sqrt(max(size, 1.0)) / 2.0
            y_lift = 0.55 + 0.018 * radius_pts
            ax.text(
                x_coord,
                y_coord + y_lift,
                short_ids.get(signature, "?"),
                color=TEXT,
                fontsize=11,
                fontweight="bold",
                ha="center",
                va="bottom",
                clip_on=False,
                zorder=4,
            )

    if positions:
        xs = [xy[0] for xy in positions.values()]
        ys = [xy[1] for xy in positions.values()]
        ax.set_xlim(min(xs) - 0.55, max(xs) + 0.55)
        # Extra headroom so labels above large top nodes are not clipped.
        ax.set_ylim(min(ys) - 0.55, max(ys) + 1.15)


def draw_stats_panel(
    ax: plt.Axes,
    current_generation: float,
    counts_map: Dict[str, float],
    metadata_df: pd.DataFrame,
    short_ids: Dict[str, str],
) -> None:
    ax.clear()
    ax.set_facecolor(PANEL)
    ax.axis("off")

    total_cells = sum(counts_map.values())
    active = {signature: value for signature, value in counts_map.items() if value > 0.5 and signature != "other"}
    dominant_signature = max(active, key=active.get) if active else ANCESTOR_SIGNATURE
    dominant_row = metadata_df.set_index("CloneSignature").loc[dominant_signature]
    dominant_cells = counts_map.get(dominant_signature, 0.0)

    text_lines = [
        ("Generation", f"{current_generation:.1f}", TEXT),
        ("Living cells", f"{int(round(total_cells)):,}".replace(",", " "), TEXT),
        ("Active clones", str(len(active) or 1), TEXT),
        (
            "Dominant clone",
            short_clone_caption(dominant_signature, short_ids),
            dominant_row["CloneColorHex"],
        ),
        ("Dominant share", f"{(dominant_cells / total_cells * 100.0) if total_cells else 0.0:.1f}%", TEXT),
    ]

    y_coord = 0.92
    ax.text(0.02, y_coord, "Current state", color=TEXT, fontsize=15, fontweight="bold", transform=ax.transAxes)
    y_coord -= 0.12

    for label, value, value_color in text_lines:
        ax.text(0.02, y_coord, label, color=SECONDARY_TEXT, fontsize=10, transform=ax.transAxes)
        ax.text(
            0.02,
            y_coord - 0.07,
            value,
            color=value_color,
            fontsize=14,
            fontweight="bold",
            transform=ax.transAxes,
        )
        y_coord -= 0.19


def render_clone_growth_animation(
    run_dir: Path,
    output_path: Path,
    fps: int,
    dpi: int,
    max_clones: int,
    max_frames: int,
    preview_frame: float | None = None,
) -> Path:
    counts_df, metadata_df, _, _ = build_clone_timecourse(run_dir, prefer_bin=False)
    counts_df, metadata_df = pick_top_clones(counts_df, metadata_df, max_clones=max_clones)

    if ANCESTOR_SIGNATURE not in set(metadata_df["CloneSignature"].astype(str)):
        counts_df[ANCESTOR_SIGNATURE] = 0.0
        metadata_df.loc[len(metadata_df)] = {
            "CloneSignature": ANCESTOR_SIGNATURE,
            "ParentSignature": ANCESTOR_SIGNATURE,
            "CloneDepth": 0,
            "FirstGeneration": float(counts_df.index.min()),
            "LastGeneration": float(counts_df.index.max()),
            "PeakCells": 0.0,
            "TotalCells": 0.0,
            "BirthTime": 0.0,
            "AddedDriverId": None,
            "CloneLabel": "ancestor",
            "CloneColorHex": "#9aa4b2",
            "IsAncestor": True,
        }

    ordered_signatures = (
        metadata_df.sort_values(
            by=["IsAncestor", "FirstGeneration", "PeakCells", "CloneSignature"],
            ascending=[False, True, False, True],
        )["CloneSignature"].tolist()
    )
    counts_df = counts_df.reindex(columns=ordered_signatures, fill_value=0.0)
    metadata_df = metadata_df.set_index("CloneSignature").loc[ordered_signatures].reset_index()

    sampled_generations, sampled_matrix = resample_counts(counts_df, max_frames=max_frames)
    colors = metadata_df["CloneColorHex"].tolist()
    positions, edges = build_tree_layout(metadata_df)
    short_ids = assign_short_clone_ids(metadata_df)

    fig = plt.figure(figsize=(15.5, 8.5), facecolor=BACKGROUND)
    # More room for the tree: narrower left plot margins + wider right column.
    grid = gridspec.GridSpec(
        2,
        2,
        figure=fig,
        width_ratios=[1.65, 1.45],
        height_ratios=[1.0, 0.72],
        left=0.04,
        right=0.99,
        top=0.94,
        bottom=0.08,
        wspace=0.12,
        hspace=0.22,
    )
    area_ax = fig.add_subplot(grid[:, 0])
    tree_ax = fig.add_subplot(grid[0, 1])
    stats_ax = fig.add_subplot(grid[1, 1])

    cumulative_max = float(sampled_matrix.sum(axis=0).max()) if sampled_matrix.size else 1.0

    def draw_frame(frame_index: int) -> None:
        current_generation = float(sampled_generations[frame_index])
        current_matrix = sampled_matrix[:, : frame_index + 1]
        current_x = sampled_generations[: frame_index + 1]
        current_counts = {
            signature: float(sampled_matrix[idx, frame_index])
            for idx, signature in enumerate(ordered_signatures)
        }

        area_ax.clear()
        area_ax.set_facecolor(PANEL)
        area_ax.stackplot(current_x, current_matrix, colors=colors, alpha=0.94, linewidth=0.0)
        min_generation = float(sampled_generations.min())
        max_generation = float(sampled_generations.max())
        if math.isclose(min_generation, max_generation):
            min_generation -= 0.5
            max_generation += 0.5
        area_ax.set_xlim(min_generation, max_generation)
        area_ax.set_ylim(0.0, cumulative_max * 1.03)
        area_ax.set_title("Clone growth over time", color=TEXT, fontsize=18, loc="left", pad=8)
        area_ax.set_xlabel("Generation", color=SECONDARY_TEXT)
        area_ax.set_ylabel("Cells", color=SECONDARY_TEXT, labelpad=2)
        area_ax.tick_params(colors=SECONDARY_TEXT, labelsize=9, pad=2)
        area_ax.yaxis.set_major_formatter(
            matplotlib.ticker.FuncFormatter(
                lambda value, _pos: f"{int(value / 1000)}k" if value >= 1000 else f"{int(value)}"
            )
        )
        area_ax.grid(True, color=GRID, linewidth=0.8, alpha=0.65)
        for spine in area_ax.spines.values():
            spine.set_color(GRID)
        area_ax.axvline(current_generation, color="#ffffff", alpha=0.28, linewidth=1.4)
        area_ax.tick_params(axis="y", which="both", length=3)
        area_ax.yaxis.get_offset_text().set_color(SECONDARY_TEXT)

        if current_counts:
            dominant_signature = max(current_counts, key=current_counts.get)
            dominant_value = current_counts[dominant_signature]
            if dominant_value > 0.0:
                share = dominant_value / max(sum(current_counts.values()), 1.0) * 100.0
                area_ax.text(
                    0.01,
                    0.97,
                    f"Leading: {short_clone_caption(dominant_signature, short_ids)}  ·  {share:.0f}%",
                    color=metadata_df.set_index("CloneSignature").loc[dominant_signature, "CloneColorHex"],
                    fontsize=11,
                    fontweight="bold",
                    transform=area_ax.transAxes,
                    va="top",
                    ha="left",
                    clip_on=True,
                )

        draw_tree_panel(
            tree_ax, metadata_df, positions, edges, current_generation, current_counts, short_ids
        )
        draw_stats_panel(stats_ax, current_generation, current_counts, metadata_df, short_ids)

    if preview_frame is not None:
        if 0.0 <= preview_frame <= 1.0 and preview_frame != 0.0:
            # Treat as fraction through the timeline unless it looks like an absolute generation.
            max_gen = float(sampled_generations.max())
            if preview_frame <= 1.0 and max_gen > 1.0:
                target_gen = float(sampled_generations.min()) + preview_frame * (
                    max_gen - float(sampled_generations.min())
                )
            else:
                target_gen = preview_frame
        else:
            target_gen = float(preview_frame)
        frame_index = int(np.argmin(np.abs(sampled_generations - target_gen)))
        draw_frame(frame_index)
        preview_path = output_path if output_path.suffix.lower() == ".png" else output_path.with_suffix(".png")
        preview_path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(preview_path, dpi=dpi, facecolor=BACKGROUND)
        plt.close(fig)
        return preview_path

    animation_obj = animation.FuncAnimation(
        fig,
        draw_frame,
        frames=len(sampled_generations),
        interval=max(1, int(1000 / max(fps, 1))),
        repeat=False,
    )

    writer, actual_output = prepare_writer(output_path, fps)
    animation_obj.save(actual_output, writer=writer, dpi=dpi)
    plt.close(fig)
    return actual_output


def main() -> None:
    args = parse_args()
    run_dir = Path(args.input).expanduser().resolve()
    if not run_dir.exists():
        raise SystemExit(f"Input directory does not exist: {run_dir}")

    output_path = Path(args.output).expanduser().resolve() if args.output else default_output_path(run_dir)
    actual_output = render_clone_growth_animation(
        run_dir=run_dir,
        output_path=output_path,
        fps=args.fps,
        dpi=args.dpi,
        max_clones=args.max_clones,
        max_frames=args.max_frames,
        preview_frame=args.preview_frame,
    )
    print(f"Saved 2D clone growth animation to: {actual_output}")


if __name__ == "__main__":
    main()
