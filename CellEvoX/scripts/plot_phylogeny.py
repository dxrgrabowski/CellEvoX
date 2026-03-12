#!/usr/bin/env python3
"""
Clone Phylogeny Visualization Module for CellEvoX

Generates a phylogenetic tree image of cell clones.
Clones are defined by driver mutations only (is_driver==true in config).

Usage:
    python plot_phylogeny.py --input <output_dir> [--output clone_tree.png]
"""

import argparse
import glob
import json
import os
import re
import sys
from typing import Dict, List, Set, Tuple

import matplotlib.pyplot as plt
import pandas as pd
import networkx as nx

def parse_mutations(mutations_str: str) -> List[Tuple[int, int]]:
    if not mutations_str or mutations_str == '""' or pd.isna(mutations_str):
        return []
    
    mutations_str = mutations_str.strip('"')
    if not mutations_str:
        return []
    
    mutations = []
    pattern = r'\((\d+),(\d+)\)'
    for match in re.finditer(pattern, mutations_str):
        mutation_id = int(match.group(1))
        mutation_type_id = int(match.group(2))
        mutations.append((mutation_id, mutation_type_id))
    
    return mutations

def get_driver_mutation_type_ids(config_path: str) -> Set[int]:
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    driver_ids = set()
    for mut in config.get('mutations', []):
        if mut.get('is_driver', False):
            driver_ids.add(mut.get('id'))
    
    return driver_ids

def get_driver_signature(mutations: List[Tuple[int, int]], driver_type_ids: Set[int]) -> str:
    driver_mutation_ids = sorted([
        mut_id for mut_id, mut_type in mutations 
        if mut_type in driver_type_ids
    ])
    
    if not driver_mutation_ids:
        return "ancestor"
    
    return ",".join(str(m) for m in driver_mutation_ids)

def find_parent_signature(signature: str, all_signatures: Set[str]) -> str:
    if signature == "ancestor":
        return ""
    
    parts = [int(x) for x in signature.split(",")]
    
    for i in range(len(parts)):
        candidate_parts = parts[:i] + parts[i+1:]
        if not candidate_parts:
            candidate = "ancestor"
        else:
            candidate = ",".join(str(m) for m in candidate_parts)
        
        if candidate in all_signatures:
            return candidate
    
    return "ancestor"

def load_population_files(output_dir: str) -> Dict[int, pd.DataFrame]:
    pop_dir = os.path.join(output_dir, "population_data")
    if os.path.isdir(pop_dir):
        pattern = os.path.join(pop_dir, "population_generation_*.csv")
    else:
        pattern = os.path.join(output_dir, "population_generation_*.csv")
        
    files = glob.glob(pattern)
    
    populations = {}
    for filepath in files:
        basename = os.path.basename(filepath)
        match = re.search(r'population_generation_(\d+)\.csv', basename)
        if match:
            generation = int(match.group(1))
            df = pd.read_csv(filepath)
            populations[generation] = df
    
    return populations

def build_clone_tree(output_dir: str) -> nx.DiGraph:
    config_path = os.path.join(output_dir, "config.json")
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"config.json not found in {output_dir}")
    
    driver_type_ids = get_driver_mutation_type_ids(config_path)
    populations = load_population_files(output_dir)
    
    if not populations:
        raise ValueError(f"No population_generation_*.csv files found in {output_dir}")
    
    # We just need to know which clone signatures exist and their max population
    all_signatures: Set[str] = set()
    node_sizes: Dict[str, int] = {}
    
    for generation in populations.values():
        for _, row in generation.iterrows():
            mutations = parse_mutations(row.get('Mutations', ''))
            signature = get_driver_signature(mutations, driver_type_ids)
            all_signatures.add(signature)
            node_sizes[signature] = node_sizes.get(signature, 0) + 1
    
    G = nx.DiGraph()
    
    for signature in all_signatures:
        title = "WT" if signature == "ancestor" else signature
        G.add_node(signature, label=title, size=node_sizes[signature])
        
        if signature != "ancestor":
            parent = find_parent_signature(signature, all_signatures)
            if parent:
                G.add_edge(parent, signature)
                
    return G

import numpy as np
import matplotlib.cm as cm
import matplotlib.pyplot as plt

def export_tree_explainability(G: nx.DiGraph, output_dir: str):
    output_file = os.path.join(output_dir, 'clones', 'clone_tree_explainability.txt')
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    with open(output_file, 'w') as f:
        f.write("=== Clone Phylogeny Tree Explainability ===\n")
        f.write("This document describes the structure of the phylogenetic tree of clones.\n\n")
        
        roots = [n for n, d in G.in_degree() if d == 0]
        if not roots:
            f.write("Error: No root found in the tree.\n")
            return
            
        def dfs_print(node, depth=0):
            indent = "  " * depth
            size = G.nodes[node].get('size', 0)
            label = G.nodes[node].get('label', node)
            f.write(f"{indent}- Clone '{label}' (Max Population: {size})\n")
            children = sorted(G.successors(node), key=lambda c: G.nodes[c].get('size', 0), reverse=True)
            for child in children:
                dfs_print(child, depth + 1)
                
        for root in roots:
            dfs_print(root)
            
    print(f"Clone phylogeny explainability saved to: {output_file}")

def plot_clone_phylogeny(output_dir: str, output_file: str = None, dpi: int = 300):
    try:
        G = build_clone_tree(output_dir)
        export_tree_explainability(G, output_dir)
    except Exception as e:
        print(f"Error building clone tree: {e}")
        return

    if len(G.nodes) == 0:
        print("Warning: No clones found to plot.")
        return

    if output_file is None:
        output_file = os.path.join(output_dir, 'phylogeny', 'clone_tree.png')
        
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    roots = [n for n, d in G.in_degree() if d == 0]
    if not roots:
        print("Warning: No roots found.")
        return
    root = roots[0]
    
    # Calculate depth natively
    sys.setrecursionlimit(50000)
    
    depths = {}
    def get_depth(node, d):
        depths[node] = d
        for child in G.successors(node):
            get_depth(child, d + 1)
    get_depth(root, 0)
    
    leaves = [n for n, d in G.out_degree() if d == 0]
    num_leaves = len(leaves)
    if num_leaves == 0:
        return
        
    dtheta = 2 * np.pi / num_leaves
    
    thetas = {}
    leaf_idx = 0
    
    def calc_theta(node):
        nonlocal leaf_idx
        children = list(G.successors(node))
        if not children:
            thetas[node] = leaf_idx * dtheta
            leaf_idx += 1
            return thetas[node]
            
        children.sort(key=lambda c: G.nodes[c].get('size', 0), reverse=True)
        child_thetas = [calc_theta(c) for c in children]
        
        thetas[node] = (min(child_thetas) + max(child_thetas)) / 2.0
        return thetas[node]
        
    calc_theta(root)
    
    # Use a larger figure size for better resolution and clarity
    fig, ax = plt.subplots(figsize=(24, 24), subplot_kw=dict(polar=True))
    ax.set_axis_off()
    
    # Use a cyclic colormap mapped exactly to the angular position (theta)
    # This ensures distinct lineages get distinct colors naturally.
    try:
        cmap = cm.get_cmap('hsv')
    except AttributeError:
        cmap = plt.get_cmap('hsv')
        
    node_colors = {}
    for node in G.nodes():
        if node == root:
            node_colors[node] = 'black'
        else:
            node_colors[node] = cmap(thetas[node] / (2 * np.pi))
            
    for u, v in G.edges():
        tu, r_u = thetas[u], depths[u]
        tv, r_v = thetas[v], depths[v]
        c = node_colors[v]
        
        t_min = min(tu, tv)
        t_max = max(tu, tv)
        if t_max - t_min > np.pi:
            if tu > tv:
                theta_vals = np.linspace(tu, tv + 2*np.pi, 50)
            else:
                theta_vals = np.linspace(tu + 2*np.pi, tv, 50)
        else:
            theta_vals = np.linspace(tu, tv, 50)
            
        r_vals = np.full_like(theta_vals, r_u)
        
        # Thicker lines for better visibility
        ax.plot(theta_vals, r_vals, color=c, linewidth=1.5, alpha=0.9)
        ax.plot([tv, tv], [r_u, r_v], color=c, linewidth=1.5, alpha=0.9)
        
    sizes = [G.nodes[n].get('size', 0) for n in G.nodes]
    if sizes:
        max_size = max(sizes)
        # Scale dots non-linearly to be readable
        nsizes = [15 + 250 * (np.log(s + 1) / np.log(max_size + 2)) for s in sizes]
    else:
        nsizes = [20] * len(G.nodes)
        
    nodes_list = list(G.nodes)
    T = [thetas[n] for n in nodes_list]
    R = [depths[n] for n in nodes_list]
    C = [node_colors[n] for n in nodes_list]
    
    # Thicker edges, larger dots
    ax.scatter(T, R, s=nsizes, c=C, zorder=3, alpha=1.0, edgecolors='black', linewidth=0.5)
    
    # Add an explanatory legend box
    textstr = (
        "Circular Clone Phylogeny\n\n"
        "• Dot Size: Proportional to log(Max Population) of the clone.\n"
        "• Colors: Radial angle-based to perfectly separate branching lineages.\n"
        "• Distance from Center: Number of accumulated driver mutations."
    )
    props = dict(boxstyle='round', facecolor='white', alpha=0.9, edgecolor='gray')
    ax.text(0.05, 0.95, textstr, transform=fig.transFigure, fontsize=20,
            verticalalignment='top', bbox=props)
    
    plt.savefig(output_file, dpi=dpi, bbox_inches='tight', transparent=False, facecolor='white')
    plt.close()
    
    print(f"Clone phylogeny tree saved to: {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate Clone Phylogeny Tree from CellEvoX simulation output'
    )
    parser.add_argument(
        '--input', '-i',
        required=True,
        help='Path to simulation output directory containing config and population data'
    )
    parser.add_argument(
        '--output', '-o',
        default=None,
        help='Output file path for the tree plot'
    )
    parser.add_argument('--dpi', type=int, default=300, help='Image DPI (default: 300)')
    
    args = parser.parse_args()
    
    if not os.path.isdir(args.input):
        print(f"Error: Input directory does not exist: {args.input}")
        sys.exit(1)
        
    if not args.output:
        args.output = os.path.join(args.input, "clones", "clone_tree.png")
        
    plot_clone_phylogeny(args.input, args.output, args.dpi)

if __name__ == '__main__':
    main()
