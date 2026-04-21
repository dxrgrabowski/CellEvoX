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
    parser.add_argument("--fps", type=int, default=6, help="Animation frame rate")
    parser.add_argument("--dpi", type=int, default=160, help="Output DPI")
    parser.add_argument(
        "--max-frames",
        type=int,
        default=140,
        help="Maximum number of snapshot checkpoints to include in the replay",
    )
    parser.add_argument(
        "--max-points",
        type=int,
        default=80000,
        help="Downsample large populations to at most this many cells per rendered frame",
    )
    parser.add_argument(
        "--pulse-frames",
        type=int,
        default=12,
        help="How many micro-frames to render per checkpoint for a slower breathing effect",
    )
    parser.add_argument(
        "--backend",
        choices=["auto", "matplotlib", "pyvista"],
        default="auto",
        help="Preferred renderer backend. Auto tries PyVista first and falls back to Matplotlib.",
    )
    parser.add_argument("--point-size", type=float, default=5.0, help="Base point size for rendered cells")
    return parser.parse_args()


def default_output_path(run_dir: Path) -> Path:
    return run_dir / "visualizations" / "tumor_growth_3d.mp4"


def prepare_writer(output_path: Path, fps: int) -> Tuple[animation.AbstractMovieWriter, Path]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    suffix = output_path.suffix.lower()
    if suffix == ".gif":
        return animation.PillowWriter(fps=fps), output_path

    if animation.writers.is_available("ffmpeg"):
        return animation.FFMpegWriter(
            fps=fps,
            metadata={"title": "CellEvoX tumor 3D replay"},
            codec="libx264",
            bitrate=3200,
        ), output_path

    fallback = output_path.with_suffix(".gif")
    print(f"Warning: ffmpeg not available, saving GIF instead: {fallback}")
    return animation.PillowWriter(fps=max(1, min(fps, 16))), fallback


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
        colors = to_rgba_array(df["CloneColorHex"].tolist(), alpha=0.88)

        frames.append(
            {
                "generation": float(frame.generation),
                "tau": float(frame.tau),
                "points": points,
                "colors": colors,
                "cell_count": int(len(df)),
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
) -> Path:
    center, radius = compute_bounds(frames)
    fig = plt.figure(figsize=(11.5, 9.0), facecolor=BACKGROUND)
    ax = fig.add_subplot(111, projection="3d")

    x_limits = (center[0] - radius, center[0] + radius)
    y_limits = (center[1] - radius, center[1] + radius)
    z_limits = (center[2] - radius, center[2] + radius)
    total_movie_frames = len(frames) * max(1, pulse_frames)

    def draw_frame(movie_frame_index: int) -> None:
        source_index = min(len(frames) - 1, movie_frame_index // max(1, pulse_frames))
        pulse_phase = (movie_frame_index % max(1, pulse_frames)) / max(1, pulse_frames)
        pulse_scale = 1.0 + 0.18 * math.sin(pulse_phase * 2.0 * math.pi)
        frame = frames[source_index]

        ax.clear()
        ax.set_facecolor(BACKGROUND)
        ax.scatter(
            frame["points"][:, 0],
            frame["points"][:, 1],
            frame["points"][:, 2],
            c=frame["colors"],
            s=point_size * pulse_scale,
            linewidths=0.0,
            depthshade=False,
        )
        ax.set_xlim(*x_limits)
        ax.set_ylim(*y_limits)
        ax.set_zlim(*z_limits)
        ax.set_box_aspect((1.0, 1.0, 1.0))
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_zticks([])

        for axis in (ax.xaxis, ax.yaxis, ax.zaxis):
            axis.pane.fill = False
            axis.pane.set_edgecolor(BACKGROUND)
            axis.line.set_color(BACKGROUND)

        orbit_fraction = movie_frame_index / max(1, total_movie_frames - 1)
        ax.view_init(
            elev=19.0 + 4.0 * math.sin(orbit_fraction * 2.0 * math.pi),
            azim=26.0 + orbit_fraction * 52.0,
        )
        ax.text2D(
            0.03,
            0.95,
            f"3D tumor replay | generation {frame['generation']:.1f}",
            transform=ax.transAxes,
            color=TEXT,
            fontsize=15,
            fontweight="bold",
        )
        ax.text2D(
            0.03,
            0.90,
            f"cells {frame['cell_count']:,} | active clones {frame['active_clones']}",
            transform=ax.transAxes,
            color=SECONDARY_TEXT,
            fontsize=11,
        )
        ax.text2D(
            0.03,
            0.05,
            "gray = ancestor / no drivers | colors = driver-defined clones",
            transform=ax.transAxes,
            color=SECONDARY_TEXT,
            fontsize=10,
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
    distance = radius * 3.4
    total_movie_frames = len(frames) * max(1, pulse_frames)
    window_size = (1440, 1080)

    plotter = pv.Plotter(off_screen=True, window_size=window_size)
    plotter.set_background(BACKGROUND)

    if actual_output.suffix.lower() == ".gif":
        plotter.open_gif(str(actual_output))
    else:
        plotter.open_movie(str(actual_output), framerate=fps)

    for movie_frame_index in range(total_movie_frames):
        source_index = min(len(frames) - 1, movie_frame_index // max(1, pulse_frames))
        pulse_phase = (movie_frame_index % max(1, pulse_frames)) / max(1, pulse_frames)
        pulse_scale = 1.0 + 0.18 * math.sin(pulse_phase * 2.0 * math.pi)
        frame = frames[source_index]

        plotter.clear_actors()
        plotter.add_points(
            frame["points"],
            scalars=frame["colors"][:, :3],
            rgb=True,
            render_points_as_spheres=True,
            point_size=point_size * pulse_scale,
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
            f"generation {frame['generation']:.1f} | cells {frame['cell_count']:,} | clones {frame['active_clones']}",
            position="upper_left",
            font_size=14,
            color=TEXT,
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
        )
    except ValueError as exc:
        print(f"Skipping 3D replay: {exc}")
        return

    print(f"Saved 3D tumor replay to: {actual_output}")


if __name__ == "__main__":
    main()
