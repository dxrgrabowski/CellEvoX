#!/usr/bin/env python3
"""
Regression tests for the trivial single-clone ("no driver mutations") handling in
plot_muller.py and plot_phylogeny.py.

Background / bugs being tested:
    For runs with no driver mutations (or where no driver mutation ever appears), the
    driver-based clone signature collapses the whole population into a single "ancestor"
    clone. build_pyfish_data() then produced an *empty, columnless* parent_tree DataFrame
    (pd.DataFrame([]) has shape (0, 0)), and pyfish's _build_tree() crashed with
    `KeyError: 'ParentId'` when indexing into it. This meant Muller plots (both relative and
    absolute) were never generated for no-driver runs, and plot_phylogeny.py rendered a
    near-blank image (a single unlabeled dot) for the equivalent single-clone case.

    Separately: a Müller diagram for that same trivial single-clone case is just one flat,
    solid-colour band -- not an informative visualization -- so instead of creating a
    (technically valid but meaningless) PNG under muller_plots/, plot_muller.py now skips the
    plot and records the reason in clones/muller_plot_skipped.txt, alongside the other
    single-clone artifacts (clone_counts_over_time.png, clone_tree_explainability.txt).

Run directly (no pytest dependency required):
    python3 CellEvoX/scripts/test_plot_muller_and_phylogeny.py
"""

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import plot_muller
import plot_phylogeny


def _write_no_driver_fixture(root: str, num_generations: int = 3, cells_per_gen: int = 5) -> None:
    """Creates a minimal on-disk run fixture with no driver mutations defined.

    Every cell only carries passenger mutations (type id 1, is_driver=False), so every cell
    maps to the same "ancestor" clone signature regardless of generation.
    """
    with open(os.path.join(root, "config.json"), "w") as f:
        f.write(
            '{"mutations": [{"id": 1, "is_driver": false, "effect": 0.0, "probability": 0.01}]}'
        )

    pop_dir = os.path.join(root, "population_data")
    os.makedirs(pop_dir, exist_ok=True)
    for generation in range(num_generations):
        path = os.path.join(pop_dir, f"population_generation_{generation}.csv")
        with open(path, "w") as f:
            f.write("CellID,ParentID,Fitness,MutationCount,Mutations,X,Y,Z,PositionValid,SpatialDimensions\n")
            for cell_id in range(cells_per_gen):
                # (mutation_id,mutation_type) pairs; mutation_type=1 is a passenger, not a driver.
                mutations = f"({cell_id + 1},1)"
                f.write(f'{cell_id},0,1.0,1,"{mutations}",,,,0,0\n')


def _write_two_clone_fixture(root: str, num_generations: int = 3) -> None:
    """Creates a fixture with one real driver mutation, producing two clone signatures
    ("ancestor" and the driver clone), to sanity-check the non-trivial path still works.
    """
    with open(os.path.join(root, "config.json"), "w") as f:
        f.write(
            '{"mutations": ['
            '{"id": 1, "is_driver": false, "effect": 0.0, "probability": 0.01},'
            '{"id": 2, "is_driver": true, "effect": 0.01, "probability": 0.001}'
            ']}'
        )

    pop_dir = os.path.join(root, "population_data")
    os.makedirs(pop_dir, exist_ok=True)
    for generation in range(num_generations):
        path = os.path.join(pop_dir, f"population_generation_{generation}.csv")
        with open(path, "w") as f:
            f.write("CellID,ParentID,Fitness,MutationCount,Mutations,X,Y,Z,PositionValid,SpatialDimensions\n")
            # Half the cells are wild-type (no driver), half carry driver mutation id=100.
            for cell_id in range(4):
                f.write(f'{cell_id},0,1.0,1,"({cell_id + 1},1)",,,,0,0\n')
            for cell_id in range(4, 8):
                f.write(f'{cell_id},0,1.0,2,"({cell_id + 1},1) (100,2)",,,,0,0\n')


class BuildPyfishDataTrivialClone(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="cellevox_muller_test_")
        self.addCleanup(shutil.rmtree, self.tmpdir, ignore_errors=True)

    def test_parent_tree_has_expected_columns_when_empty(self):
        _write_no_driver_fixture(self.tmpdir)

        populations_df, parent_tree_df = plot_muller.build_pyfish_data(self.tmpdir)

        # This is the crux of the original bug: an empty DataFrame built without an explicit
        # `columns=` argument has shape (0, 0), so `parent_tree_df["ParentId"]` raises KeyError
        # deep inside pyfish. With the fix, the columns must always be present.
        self.assertEqual(list(parent_tree_df.columns), ["ParentId", "ChildId"])
        self.assertTrue(parent_tree_df.empty)
        self.assertFalse(populations_df.empty)

        # Must not raise KeyError: 'ParentId'.
        parent_tree_df["ParentId"]

    def test_plot_muller_diagram_skips_plot_and_writes_clones_note(self):
        _write_no_driver_fixture(self.tmpdir)
        output_file = os.path.join(self.tmpdir, "muller_plots", "muller_plot.png")

        plot_muller.plot_muller_diagram(self.tmpdir, output_file=output_file)

        # No PNG should be written for the trivial single-clone case...
        self.assertFalse(os.path.exists(output_file))

        # ...instead, an explanatory note lives alongside the other single-clone artifacts.
        note_path = os.path.join(self.tmpdir, "clones", "muller_plot_skipped.txt")
        self.assertTrue(os.path.exists(note_path))
        with open(note_path) as f:
            contents = f.read()
        self.assertIn("Müller plot skipped", contents)

    def test_plot_muller_diagram_absolute_variant_also_skips(self):
        _write_no_driver_fixture(self.tmpdir)
        output_file = os.path.join(self.tmpdir, "muller_plots", "muller_plot_absolute.png")

        plot_muller.plot_muller_diagram(self.tmpdir, output_file=output_file, absolute=True)

        self.assertFalse(os.path.exists(output_file))
        self.assertTrue(os.path.exists(os.path.join(self.tmpdir, "clones", "muller_plot_skipped.txt")))

    def test_two_clone_fixture_still_produces_a_real_parent_edge(self):
        """Sanity check that the fix does not regress the normal multi-clone case."""
        self.tmpdir2 = tempfile.mkdtemp(prefix="cellevox_muller_test2_")
        self.addCleanup(shutil.rmtree, self.tmpdir2, ignore_errors=True)
        _write_two_clone_fixture(self.tmpdir2)

        populations_df, parent_tree_df = plot_muller.build_pyfish_data(self.tmpdir2)

        self.assertEqual(list(parent_tree_df.columns), ["ParentId", "ChildId"])
        self.assertEqual(len(parent_tree_df), 1)  # one driver clone parented by "ancestor"

        output_file = os.path.join(self.tmpdir2, "muller_plot.png")
        plot_muller.plot_muller_diagram(self.tmpdir2, output_file=output_file)
        self.assertTrue(os.path.exists(output_file))


class PlotClonePhylogenyTrivialClone(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="cellevox_phylogeny_test_")
        self.addCleanup(shutil.rmtree, self.tmpdir, ignore_errors=True)

    def test_build_clone_tree_has_single_node(self):
        _write_no_driver_fixture(self.tmpdir)
        graph = plot_phylogeny.build_clone_tree(self.tmpdir)
        self.assertEqual(len(graph.nodes), 1)

    def test_plot_clone_phylogeny_renders_single_clone_placeholder(self):
        _write_no_driver_fixture(self.tmpdir)
        output_file = os.path.join(self.tmpdir, "phylogeny", "clone_tree.png")

        plot_phylogeny.plot_clone_phylogeny(self.tmpdir, output_file=output_file)

        self.assertTrue(os.path.exists(output_file))
        self.assertGreater(os.path.getsize(output_file), 0)

    def test_two_clone_fixture_still_renders_full_tree(self):
        _write_two_clone_fixture(self.tmpdir)
        output_file = os.path.join(self.tmpdir, "phylogeny", "clone_tree.png")

        graph = plot_phylogeny.build_clone_tree(self.tmpdir)
        self.assertEqual(len(graph.nodes), 2)

        plot_phylogeny.plot_clone_phylogeny(self.tmpdir, output_file=output_file)
        self.assertTrue(os.path.exists(output_file))


if __name__ == "__main__":
    unittest.main()
