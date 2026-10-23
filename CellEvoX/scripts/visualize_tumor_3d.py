#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib

matplotlib.use("Agg")

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import to_rgba_array

from snapshot_io import PopulationFrameSource, discover_population_sources, load_driver_type_ids, load_population_frame


BACKGROUND = "#090b10"
TEXT = "#f1f4f8"
SECONDARY_TEXT = "#aeb6c2"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a 3D tumor replay from CellEvoX snapshots.")
    parser.add_argument("--input", required=True, help="Path to a simulation output directory")
    parser.add_argument(
        "--output",
        default=None,
        help="Output movie path (default: <input>/visualizations/tumor_growth_3d.mp4)",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=30,
        help="Animation frame rate (with default pulse-frames=2 → 15 snapshots/s)",
    )
    parser.add_argument("--dpi", type=int, default=160, help="Output DPI")
    parser.add_argument(
        "--max-frames",
        type=int,
        default=250,
        help="Maximum number of snapshot checkpoints to include in the replay",
    )
    parser.add_argument(
        "--max-points",
        type=int,
        default=45000,
        help="Downsample large populations to at most this many cells per rendered frame",
    )
    parser.add_argument(
        "--pulse-frames",
        type=int,
        default=2,
        help="Movie frames held per snapshot (default 2 @ 30fps = 15 snapshots/s)",
    )
    parser.add_argument(
        "--backend",
        choices=["auto", "matplotlib", "pyvista"],
        default="auto",
        help="Preferred renderer backend. Auto tries PyVista first and falls back to Matplotlib.",
    )
    parser.add_argument("--point-size", type=float, default=2.6, help="Base point size for rendered cells")
    parser.add_argument(
        "--view-zoom",
        type=float,
        default=1.35,
        help="Zoom factor for the 3D cloud in-frame (matplotlib; 1.0 = neutral)",
    )
    parser.add_argument(
        "--box-aspect",
        default="1:1:1",
        help="3D axes box aspect as W:D:H (default 1:1:1 cube = true relative distances)",
    )
    return parser.parse_args()


def parse_box_aspect(raw: str) -> Tuple[float, float, float]:
    parts = [p.strip() for p in str(raw).replace(",", ":").split(":") if p.strip()]
    if len(parts) != 3:
        raise ValueError(f"Invalid --box-aspect '{raw}', expected W:D:H like 16:9:9")
    values = tuple(float(part) for part in parts)
    if any(value <= 0.0 for value in values):
        raise ValueError(f"Invalid --box-aspect '{raw}', all components must be > 0")
    return values  # type: ignore[return-value]


def default_output_path(run_dir: Path) -> Path:
    return run_dir / "visualizations" / "tumor_growth_3d.mp4"


def prepare_writer(output_path: Path, fps: int) -> Tuple[animation.AbstractMovieWriter, Path]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    suffix = output_path.suffix.lower()
    if suffix == ".gif":
        return animation.PillowWriter(fps=fps), output_path

    if animation.writers.is_available("ffmpeg"):
        # CRF keeps dense particle frames sharper than a low fixed bitrate.
        return animation.FFMpegWriter(
            fps=fps,
            metadata={"title": "CellEvoX tumor 3D replay"},
            codec="libx264",
            extra_args=["-pix_fmt", "yuv420p", "-crf", "17", "-preset", "medium"],
        ), output_path

    fallback = output_path.with_suffix(".gif")
    print(f"Warning: ffmpeg not available, saving GIF instead: {fallback}")
    return animation.PillowWriter(fps=max(1, min(fps, 16))), fallback


def adaptive_point_style(cell_count: int, base_size: float) -> Tuple[float, float]:
    """Shrink markers and raise opacity as density grows to avoid muddy overlap."""
    # Reference: look crisp around a few thousand cells; taper after that.
    density = max(cell_count, 1) / 2500.0
    size = base_size / math.sqrt(max(density, 1.0))
    size = float(np.clip(size, 0.55, base_size * 1.15))
    # Prefer nearly opaque markers; only a little transparency when sparse.
    alpha = float(np.clip(0.55 + 0.40 / math.sqrt(max(density, 1.0)), 0.55, 0.95))
    return size, alpha


def apply_alpha(colors: np.ndarray, alpha: float) -> np.ndarray:
    tinted = np.array(colors, dtype=float, copy=True)
    if tinted.ndim != 2 or tinted.shape[1] < 4:
        return tinted
    tinted[:, 3] = alpha
    return tinted


def pick_sources(run_dir: Path, max_frames: int) -> List:
    sources = discover_population_sources(run_dir, prefer_bin=True)
    if not sources:
        raise ValueError(f"No population snapshots found in {run_dir}")
    if len(sources) <= max_frames:
        return sources

    indices = np.linspace(0, len(sources) - 1, num=max_frames, dtype=int)
    deduplicated = []
    seen = set()
    for index in indices.tolist():
        if index not in seen:
            deduplicated.append(sources[index])
            seen.add(index)
    return deduplicated


def downsample_frame(df, max_points: int):
    if len(df) <= max_points:
        return df
    ordered = df.sort_values("CellID").reset_index(drop=True)
    stride = max(1, int(math.ceil(len(ordered) / max_points)))
    return ordered.iloc[::stride].head(max_points).reset_index(drop=True)


def load_spatial_frames(run_dir: Path, max_frames: int, max_points: int) -> List[Dict]:
    driver_type_ids = load_driver_type_ids(run_dir)
    frames: List[Dict] = []

    for source in pick_sources(run_dir, max_frames=max_frames):
        try:
            frame = load_population_frame(source, driver_type_ids)
        except ValueError as exc:
            if source.kind != "bin":
                raise
            csv_path = source.path.with_suffix(".csv")
            if not csv_path.exists():
                raise
            print(f"Warning: falling back to CSV for {source.path.name} ({exc})")
            frame = load_population_frame(
                PopulationFrameSource(generation=source.generation, path=csv_path, kind="csv"),
                driver_type_ids,
            )
        if int(frame.spatial_dimensions) != 3:
            continue

        df = frame.data.copy()
        if "PositionValid" in df.columns:
            df = df[df["PositionValid"].astype(int) == 1]
        if df.empty:
            continue

        df = df.dropna(subset=["X", "Y", "Z"]).reset_index(drop=True)
        if df.empty:
            continue

        df = downsample_frame(df, max_points=max_points)
        points = df[["X", "Y", "Z"]].to_numpy(dtype=float)
        cell_count = int(len(df))
        _, alpha = adaptive_point_style(cell_count, base_size=4.0)
        colors = to_rgba_array(df["CloneColorHex"].tolist(), alpha=alpha)

        frames.append(
            {
                "generation": float(frame.generation),
                "tau": float(frame.tau),
                "points": points,
                "colors": colors,
                "cell_count": cell_count,
                "active_clones": int(df["CloneSignature"].nunique()),
            }
        )

    if not frames:
        raise ValueError("No spatial 3D checkpoints with valid positions were found.")
    return frames


def compute_bounds(frames: List[Dict]) -> Tuple[np.ndarray, float]:
    all_points = np.vstack([frame["points"] for frame in frames if len(frame["points"]) > 0])
    mins = all_points.min(axis=0)
    maxs = all_points.max(axis=0)
    center = (mins + maxs) / 2.0
    radius = float(np.max(maxs - mins) / 2.0)
    radius = max(radius, 1.0)
    return center, radius


def render_with_matplotlib(
    frames: List[Dict],
    output_path: Path,
    fps: int,
    dpi: int,
    point_size: float,
    pulse_frames: int,
    view_zoom: float = 1.35,
    box_aspect: Tuple[float, float, float] = (1.0, 1.0, 1.0),
) -> Path:
    center, radius = compute_bounds(frames)
    # Widescreen 16:9 canvas.
    fig = plt.figure(figsize=(16.0, 9.0), facecolor=BACKGROUND)
    fig.subplots_adjust(left=0.0, right=1.0, bottom=0.0, top=1.0)
    ax = fig.add_subplot(111, projection="3d")
    # Push the prism toward the left/right edges.
    ax.set_position([0.01, 0.04, 0.98, 0.92])

    view_radius = max(radius * 1.0, 1.0)
    x_limits = (center[0] - view_radius, center[0] + view_radius)
    y_limits = (center[1] - view_radius, center[1] + view_radius)
    z_limits = (center[2] - view_radius, center[2] + view_radius)
    total_movie_frames = len(frames) * max(1, pulse_frames)
    zoom = max(0.8, float(view_zoom))
    aspect = tuple(float(v) for v in box_aspect)

    label_bbox = {
        "boxstyle": "round,pad=0.28",
        "facecolor": "#0c0f16",
        "edgecolor": "#2a3140",
        "alpha": 0.90,
    }

    def clear_fig_labels() -> None:
        for artist in list(fig.texts):
            artist.remove()

    def draw_frame(movie_frame_index: int) -> None:
        source_index = min(len(frames) - 1, movie_frame_index // max(1, pulse_frames))
        # Hold each checkpoint; tiny size pulse only (large pulse looked like motion blur).
        pulse_phase = (movie_frame_index % max(1, pulse_frames)) / max(1, pulse_frames)
        pulse_scale = 1.0 + 0.04 * math.sin(pulse_phase * 2.0 * math.pi)
        frame = frames[source_index]
        marker_size, alpha = adaptive_point_style(frame["cell_count"], point_size)
        colors = apply_alpha(frame["colors"], alpha)

        ax.clear()
        clear_fig_labels()
        ax.set_facecolor(BACKGROUND)
        ax.set_position([0.01, 0.04, 0.98, 0.92])
        ax.scatter(
            frame["points"][:, 0],
            frame["points"][:, 1],
            frame["points"][:, 2],
            c=colors,
            s=marker_size * pulse_scale,
            linewidths=0.0,
            depthshade=False,
            rasterized=True,
        )
        ax.set_xlim(*x_limits)
        ax.set_ylim(*y_limits)
        ax.set_zlim(*z_limits)
        # Wide rectangular prism (not a cube) so the cloud stretches sideways in 16:9.
        try:
            ax.set_box_aspect(aspect, zoom=zoom)
        except TypeError:
            ax.set_box_aspect(aspect)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_zticks([])
        ax.grid(False)

        for axis in (ax.xaxis, ax.yaxis, ax.zaxis):
            axis.pane.fill = False
            axis.pane.set_facecolor(BACKGROUND)
            axis.pane.set_edgecolor(BACKGROUND)
            axis.pane.set_alpha(0.0)
            axis.line.set_color(BACKGROUND)
            axis.line.set_linewidth(0.0)

        # Continuous orbit across the whole movie (does not reset on generation change).
        orbit_fraction = movie_frame_index / max(1, total_movie_frames - 1)
        ax.view_init(
            elev=19.0 + 4.0 * math.sin(orbit_fraction * 2.0 * math.pi),
            azim=26.0 + orbit_fraction * 52.0,
        )
        # HUD in true figure corners (not the inset 3D axes box).
        fig.text(
            0.012,
            0.985,
            f"3D tumor replay | generation {frame['generation']:.0f}",
            transform=fig.transFigure,
            color=TEXT,
            fontsize=14,
            fontweight="bold",
            ha="left",
            va="top",
            bbox=label_bbox,
            zorder=10,
        )
        fig.text(
            0.012,
            0.935,
            f"cells {frame['cell_count']:,} | active clones {frame['active_clones']}",
            transform=fig.transFigure,
            color=SECONDARY_TEXT,
            fontsize=11,
            ha="left",
            va="top",
            bbox=label_bbox,
            zorder=10,
        )
        fig.text(
            0.012,
            0.018,
            "gray = ancestor / no drivers | colors = driver-defined clones",
            transform=fig.transFigure,
            color=SECONDARY_TEXT,
            fontsize=9,
            ha="left",
            va="bottom",
            bbox=label_bbox,
            zorder=10,
        )

    animation_obj = animation.FuncAnimation(
        fig,
        draw_frame,
        frames=total_movie_frames,
        interval=max(1, int(1000 / max(fps, 1))),
        repeat=False,
    )
    writer, actual_output = prepare_writer(output_path, fps)
    animation_obj.save(actual_output, writer=writer, dpi=dpi)
    plt.close(fig)
    return actual_output


def render_with_pyvista(
    frames: List[Dict],
    output_path: Path,
    fps: int,
    point_size: float,
    pulse_frames: int,
) -> Path:
    import pyvista as pv

    output_path.parent.mkdir(parents=True, exist_ok=True)
    actual_output = output_path
    if actual_output.suffix.lower() not in {".mp4", ".gif"}:
        actual_output = actual_output.with_suffix(".mp4")

    center, radius = compute_bounds(frames)
    distance = radius * 2.55  # closer camera → larger tumor in frame
    total_movie_frames = len(frames) * max(1, pulse_frames)
    window_size = (1920, 1080)

    plotter = pv.Plotter(off_screen=True, window_size=window_size)
    plotter.set_background(BACKGROUND)

    if actual_output.suffix.lower() == ".gif":
        plotter.open_gif(str(actual_output))
    else:
        plotter.open_movie(str(actual_output), framerate=fps)

    for movie_frame_index in range(total_movie_frames):
        source_index = min(len(frames) - 1, movie_frame_index // max(1, pulse_frames))
        pulse_phase = (movie_frame_index % max(1, pulse_frames)) / max(1, pulse_frames)
        pulse_scale = 1.0 + 0.04 * math.sin(pulse_phase * 2.0 * math.pi)
        frame = frames[source_index]
        marker_size, _alpha = adaptive_point_style(frame["cell_count"], point_size)

        plotter.clear_actors()
        plotter.add_points(
            frame["points"],
            scalars=frame["colors"][:, :3],
            rgb=True,
            render_points_as_spheres=True,
            # PyVista point_size is in screen pixels; keep denser clouds smaller.
            point_size=max(2.0, marker_size * pulse_scale * 1.6),
        )

        orbit_fraction = movie_frame_index / max(1, total_movie_frames - 1)
        angle = math.radians(25.0 + orbit_fraction * 70.0)
        camera_position = np.array(
            [
                center[0] + math.cos(angle) * distance,
                center[1] + math.sin(angle) * distance,
                center[2] + distance * 0.42,
            ]
        )
        plotter.camera_position = [camera_position.tolist(), center.tolist(), [0.0, 0.0, 1.0]]
        plotter.add_text(
            f"generation {frame['generation']:.0f} | cells {frame['cell_count']:,} | clones {frame['active_clones']}",
            position="upper_left",
            font_size=14,
            color=TEXT,
            shadow=True,
        )
        plotter.write_frame()

    plotter.close()
    return actual_output


def render_tumor_replay(
    run_dir: Path,
    output_path: Path,
    fps: int,
    dpi: int,
    max_frames: int,
    max_points: int,
    pulse_frames: int,
    backend: str,
    point_size: float,
    view_zoom: float = 1.35,
    box_aspect: Tuple[float, float, float] = (1.0, 1.0, 1.0),
) -> Path:
    frames = load_spatial_frames(run_dir, max_frames=max_frames, max_points=max_points)

    if backend in {"auto", "pyvista"}:
        try:
            return render_with_pyvista(
                frames=frames,
                output_path=output_path,
                fps=fps,
                point_size=point_size,
                pulse_frames=pulse_frames,
            )
        except Exception as exc:
            if backend == "pyvista":
                raise
            print(f"Warning: PyVista renderer unavailable, falling back to Matplotlib ({exc})")

    return render_with_matplotlib(
        frames=frames,
        output_path=output_path,
        fps=fps,
        dpi=dpi,
        point_size=point_size,
        pulse_frames=pulse_frames,
        view_zoom=view_zoom,
        box_aspect=box_aspect,
    )


def main() -> None:
    args = parse_args()
    run_dir = Path(args.input).expanduser().resolve()
    if not run_dir.exists():
        raise SystemExit(f"Input directory does not exist: {run_dir}")

    output_path = Path(args.output).expanduser().resolve() if args.output else default_output_path(run_dir)
    try:
        actual_output = render_tumor_replay(
            run_dir=run_dir,
            output_path=output_path,
            fps=args.fps,
            dpi=args.dpi,
            max_frames=args.max_frames,
            max_points=args.max_points,
            pulse_frames=max(1, args.pulse_frames),
            backend=args.backend,
            point_size=max(0.1, args.point_size),
            view_zoom=max(0.8, args.view_zoom),
            box_aspect=parse_box_aspect(args.box_aspect),
        )
    except ValueError as exc:
        print(f"Skipping 3D replay: {exc}")
        return

    print(f"Saved 3D tumor replay to: {actual_output}")


if __name__ == "__main__":
    main()
