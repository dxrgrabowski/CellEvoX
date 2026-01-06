#!/usr/bin/env python3
"""
Müller Plot Visualization Module for CellEvoX

Generates Müller plots (fishplots) showing the evolutionary history of cell clones.
Clones are defined by driver mutations only (is_driver==true in config).

Usage:
    python plot_muller.py --input <output_dir> [--output muller_plot.png]

Requirements:
    pip install pyfish pandas matplotlib
"""

import argparse
import glob
import json
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

try:
    from pyfish import fish_plot, process_data, setup_figure
except ImportError:
    print("Error: pyfish not installed. Run: pip install pyfish")
    sys.exit(1)


def parse_mutations(mutations_str: str) -> List[Tuple[int, int]]:
    """
    Parse mutations string from CSV.
    Format: "(mutation_id,mutation_type_id) (mutation_id,mutation_type_id) ..."
    Returns list of (mutation_id, mutation_type_id) tuples.
    """
    if not mutations_str or mutations_str == '""' or pd.isna(mutations_str):
        return []
    
    # Remove surrounding quotes if present
    mutations_str = mutations_str.strip('"')
    if not mutations_str:
        return []
    
    mutations = []
    # Match pattern (id,type)
    pattern = r'\((\d+),(\d+)\)'
    for match in re.finditer(pattern, mutations_str):
        mutation_id = int(match.group(1))
        mutation_type_id = int(match.group(2))
        mutations.append((mutation_id, mutation_type_id))
    
    return mutations


def get_driver_mutation_type_ids(config_path: str) -> Set[int]:
    """
    Read config.json and return set of mutation type IDs that are drivers.
    """
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    driver_ids = set()
    for mut in config.get('mutations', []):
        if mut.get('is_driver', False):
            driver_ids.add(mut.get('id'))
    
    return driver_ids


def get_driver_signature(mutations: List[Tuple[int, int]], driver_type_ids: Set[int]) -> str:
    """
    Extract driver mutations and return a signature string.
    Returns sorted, comma-separated mutation_ids for driver mutations only.
    """
    driver_mutation_ids = sorted([
        mut_id for mut_id, mut_type in mutations 
        if mut_type in driver_type_ids
    ])
    
    if not driver_mutation_ids:
        return "ancestor"  # No driver mutations = ancestral clone
    
    return ",".join(str(m) for m in driver_mutation_ids)


def find_parent_signature(signature: str, all_signatures: Set[str]) -> str:
    """
    Find the parent clone signature.
    Parent is the signature with one less driver mutation that is a subset.
    """
    if signature == "ancestor":
        return ""
    
    parts = [int(x) for x in signature.split(",")]
    
    # Try removing each mutation to find a valid parent
    for i in range(len(parts)):
        candidate_parts = parts[:i] + parts[i+1:]
        if not candidate_parts:
            candidate = "ancestor"
        else:
            candidate = ",".join(str(m) for m in candidate_parts)
        
        if candidate in all_signatures:
            return candidate
    
    # If no subset found, parent is ancestor
    return "ancestor"


def load_population_files(output_dir: str) -> Dict[int, pd.DataFrame]:
    """
    Load all population_generation_*.csv files.
    Returns dict mapping generation -> DataFrame.
    """
    # Check for population_data subdirectory
    pop_dir = os.path.join(output_dir, "population_data")
    if os.path.isdir(pop_dir):
        pattern = os.path.join(pop_dir, "population_generation_*.csv")
    else:
        pattern = os.path.join(output_dir, "population_generation_*.csv")
        
    files = glob.glob(pattern)
    
    populations = {}
    for filepath in files:
        # Extract generation number from filename
        basename = os.path.basename(filepath)
        match = re.search(r'population_generation_(\d+)\.csv', basename)
        if match:
            generation = int(match.group(1))
            df = pd.read_csv(filepath)
            populations[generation] = df
    
    return populations


def build_pyfish_data(output_dir: str) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """
    Build populations and parent_tree DataFrames for pyfish.
    
    Pyfish format:
    - populations: columns [Id, Step, Pop] where Id=clone_id, Step=generation, Pop=count
    - parent_tree: columns [ParentId, ChildId]
    """
    # Find config.json in output directory
    config_path = os.path.join(output_dir, "config.json")
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"config.json not found in {output_dir}")
    
    driver_type_ids = get_driver_mutation_type_ids(config_path)
    print(f"Driver mutation type IDs: {driver_type_ids}")
    
    # Load all population files
    populations = load_population_files(output_dir)
    if not populations:
        raise ValueError(f"No population_generation_*.csv files found in {output_dir}")
    
    print(f"Found {len(populations)} generation files")
    
    # Handle single generation case (prevents pyfish crash)
    if len(populations) == 1:
        single_gen = list(populations.keys())[0]
        # Create a second time point slightly after to allow plotting
        # If gen is 0, make it 1. If gen is 100, make it 101.
        next_gen = single_gen + (1 if single_gen == 0 else single_gen * 0.1)
        # Round to integer if possible, but float is fine for keys usually, 
        # but let's stick to int if the original keys are ints (which they are)
        next_gen = single_gen + 1
        
        print(f"WARNING: Only one generation data found ({single_gen}). Duplicating as {next_gen} to enable plotting.")
        populations[next_gen] = populations[single_gen].copy()
    
    # Track all clone signatures and their populations per generation
    clone_populations: Dict[int, Dict[str, int]] = {}  # generation -> {signature -> count}
    all_signatures: Set[str] = set()
    
    for generation in sorted(populations.keys()):
        df = populations[generation]
        clone_populations[generation] = {}
        
        for _, row in df.iterrows():
            mutations = parse_mutations(row.get('Mutations', ''))
            signature = get_driver_signature(mutations, driver_type_ids)
            all_signatures.add(signature)
            
            clone_populations[generation][signature] = \
                clone_populations[generation].get(signature, 0) + 1
    
    print(f"Found {len(all_signatures)} unique clone signatures")
    
    # Create mapping from signature to numeric ID
    signature_to_id = {sig: idx for idx, sig in enumerate(sorted(all_signatures))}
    
    # Build populations DataFrame for pyfish
    # Format: Id (clone_id), Step (generation), Pop (count)
    pop_rows = []
    for generation in sorted(clone_populations.keys()):
        for signature in all_signatures:
            count = clone_populations[generation].get(signature, 0)
            if count > 0 or generation == min(clone_populations.keys()) or generation == max(clone_populations.keys()):
                # Include all clones at first and last generation, and non-zero in between
                pop_rows.append({
                    'Id': signature_to_id[signature],
                    'Step': generation,
                    'Pop': count
                })
    
    populations_df = pd.DataFrame(pop_rows)
    # Cast Pop to float to avoid FutureWarning in pyfish when it divides by 2
    populations_df['Pop'] = populations_df['Pop'].astype(float)
    
    # Build parent_tree DataFrame for pyfish
    # Format: ParentId, ChildId (both using numeric IDs)
    parent_rows = []
    for signature in all_signatures:
        if signature == "ancestor":
            continue  # Ancestor has no parent
        parent_sig = find_parent_signature(signature, all_signatures)
        if parent_sig:  # parent_sig could be empty string if no parent found
            parent_rows.append({
                'ParentId': signature_to_id[parent_sig],
                'ChildId': signature_to_id[signature]
            })
    
    parent_tree_df = pd.DataFrame(parent_rows)
    
    # Save signature mapping for reference
    signature_map = pd.DataFrame([
        {'CloneId': clone_id, 'Signature': sig}
        for sig, clone_id in signature_to_id.items()
    ])
    signature_map.to_csv(os.path.join(output_dir, 'clone_id_mapping.csv'), index=False)
    
    return populations_df, parent_tree_df


def plot_muller_diagram(output_dir: str, output_file: str = None, absolute: bool = False):
    """
    Generate and save the Müller plot using pyfish.
    """
    print(f"Processing data from: {output_dir}")
    
    populations_df, parent_tree_df = build_pyfish_data(output_dir)
    
    # Save intermediate data for debugging
    populations_df.to_csv(os.path.join(output_dir, 'pyfish_populations.csv'), index=False)
    parent_tree_df.to_csv(os.path.join(output_dir, 'pyfish_parent_tree.csv'), index=False)
    print(f"Saved pyfish_populations.csv and pyfish_parent_tree.csv")
    
    if populations_df.empty:
        print("Warning: No population data to plot")
        return
    
    if output_file is None:
        output_file = os.path.join(output_dir, 'muller_plot.png')
    
    # Generate Müller plot using pyfish
    try:
        print(f"Populations DataFrame shape: {populations_df.shape}")
        print(f"Parent tree DataFrame shape: {parent_tree_df.shape}")
        print(f"Sample populations:\n{populations_df.head(20)}")
        print(f"Parent tree:\n{parent_tree_df.head(20)}")
        
        # Process data for pyfish
        data = process_data(populations_df, parent_tree_df, absolute=absolute)
        
        # Create figure manually to avoid font size issues
        fig, ax = plt.subplots(figsize=(14, 8))
        
        # Create fish plot
        fish_plot(*data, ax=ax)
        
        ylabel = 'Absolute Population' if absolute else 'Relative Population'
        ax.set_xlabel('Generation', fontsize=12)
        ax.set_ylabel(ylabel, fontsize=12)
        title = 'Müller Plot: Clone Evolution (Absolute Counts)' if absolute else 'Müller Plot: Clone Evolution'
        ax.set_title(title, fontsize=14)
        
        fig.savefig(output_file, dpi=150)
        plt.close()
        
        print(f"Müller plot saved to: {output_file}")
        
    except Exception as e:
        print(f"Error generating Müller plot: {e}")
        import traceback
        traceback.print_exc()
        raise


def main():
    parser = argparse.ArgumentParser(
        description='Generate Müller plot from CellEvoX simulation output'
    )
    parser.add_argument(
        '--input', '-i',
        required=True,
        help='Path to simulation output directory containing population CSV files'
    )
    parser.add_argument(
        '--output', '-o',
        default=None,
        help='Output file path for the plot (default: <input>/muller_plot.png)'
    )
    parser.add_argument(
        '--absolute', '-a',
        action='store_true',
        help='Show absolute population counts instead of relative/stacked'
    )
    
    args = parser.parse_args()
    
    if not os.path.isdir(args.input):
        print(f"Error: Input directory does not exist: {args.input}")
        sys.exit(1)
    
    plot_muller_diagram(args.input, args.output, args.absolute)


if __name__ == '__main__':
    main()
