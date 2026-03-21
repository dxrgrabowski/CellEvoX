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
import random
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


def export_cell_tree_explainability(G: nx.DiGraph, depths: dict, output_dir: str, num_cells: int):
    output_file = os.path.join(output_dir, 'phylogeny', f'cell_tree_{num_cells}_explainability.txt')
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    with open(output_file, 'w') as f:
        f.write(f"=== Cell Phylogeny Tree Explainability ({num_cells} cells) ===\n")
        f.write("This document describes the structure of the phylogenetic tree of individual cells sampled from the final population.\n")
        f.write("Depths in this tree represent the Generation (Time) of branching points.\n\n")
        
        roots = [n for n, d in G.in_degree() if d == 0]
        if not roots:
            f.write("Error: No root found in the tree.\n")
            return
            
        def dfs_print(node, depth_lvl=0):
            indent = "  " * depth_lvl
            time = depths.get(node, 0.0)
            if G.out_degree(node) > 0:
                label = f"Branch Point (Gen: {time:.2f}, ID: {node})"
            else:
                label = f"Cell {node} (Final Gen: {time:.2f})"
            f.write(f"{indent}- {label}\n")
            children = sorted(G.successors(node), key=lambda x: str(x))
            for child in children:
                dfs_print(child, depth_lvl + 1)
                
        for root in roots:
            dfs_print(root)
            
    print(f"Cell phylogeny explainability saved to: {output_file}")

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

def build_cell_tree(output_dir: str, num_cells: int) -> Tuple[nx.DiGraph, Dict[int, float]]:
    tree_file = os.path.join(output_dir, "phylogeny", "phylogenetic_tree.csv")
    if not os.path.isfile(tree_file):
        raise ValueError(f"Required file {tree_file} not found.")
        
    pop_dir = os.path.join(output_dir, "population_data")
    pattern = os.path.join(pop_dir, "population_generation_*.csv")
    files = glob.glob(pattern)
    if not files:
        raise ValueError(f"No population_generation_*.csv files found in {pop_dir}")
        
    def get_gen(filepath):
        match = re.search(r'population_generation_(\d+)\.csv', os.path.basename(filepath))
        return int(match.group(1)) if match else -1
        
    files.sort(key=get_gen)
    final_file = files[-1]
    final_gen = get_gen(final_file)
    
    df_final = pd.read_csv(final_file)
    if len(df_final) <= num_cells:
        print(f"Warning: requested {num_cells} cells, but final population only has {len(df_final)}. Using all.")
        sampled_cells = df_final['CellID'].tolist()
    else:
        sampled_cells = df_final.sample(n=num_cells, random_state=42)['CellID'].tolist()
        
    tree_df = pd.read_csv(tree_file)
    id_to_parent = dict(zip(tree_df['NodeID'], tree_df['ParentID']))
    id_to_death = dict(zip(tree_df['NodeID'], tree_df['DeathTime']))
    
    G = nx.DiGraph()
    depths = {}
    
    visited = set()
    for leaf_id in sampled_cells:
        curr = leaf_id
        depths[curr] = final_gen
        
        while curr != 0:
            parent = id_to_parent.get(curr)
            if parent is None:
                # If parent not in tree, attach to root 0
                parent = 0
            
            G.add_edge(parent, curr)
            
            if parent not in depths:
                depths[parent] = id_to_death.get(parent, 0.0)
                
            if parent in visited:
                break
            
            visited.add(parent)
            curr = parent
            
    return G, depths

def plot_cell_phylogeny(output_dir: str, num_cells: int, output_file: str = None, dpi: int = 300, use_topological: bool = True):
    try:
        G, node_generations = build_cell_tree(output_dir, num_cells)
        export_cell_tree_explainability(G, node_generations, output_dir, num_cells)
    except Exception as e:
        print(f"Error building cell tree: {e}")
        import traceback
        traceback.print_exc()
        return

    if len(G.nodes) == 0:
        print("Warning: No cells found to plot.")
        return

    if use_topological:
        # Radial distance = topological depth (number of edges from root)
        node_depths = nx.shortest_path_length(G, source=0)
    else:
        # Radial distance = generation (absolute time)
        node_depths = node_generations

    if output_file is None:
        suffix = "topological" if use_topological else "linear"
        output_file = os.path.join(output_dir, 'phylogeny', f'cell_tree_{num_cells}_{suffix}.png')
        
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    roots = [n for n, d in G.in_degree() if d == 0]
    if not roots:
        print("Warning: No roots found.")
        return
    root = roots[0]
    
    sys.setrecursionlimit(500000)
    
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
            
        child_thetas = [calc_theta(c) for c in children]
        thetas[node] = (min(child_thetas) + max(child_thetas)) / 2.0
        return thetas[node]
        
    calc_theta(root)
    
    fig, ax = plt.subplots(figsize=(24, 24), subplot_kw=dict(polar=True))
    ax.set_axis_off()
    
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
        tu, r_u = thetas[u], node_depths[u]
        tv, r_v = thetas[v], node_depths[v]
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
        
        ax.plot(theta_vals, r_vals, color=c, linewidth=1.0, alpha=0.8)
        ax.plot([tv, tv], [r_u, r_v], color=c, linewidth=1.0, alpha=0.8)
        
    nodes_list = list(G.nodes)
    T = [thetas[n] for n in nodes_list]
    R = [node_depths[n] for n in nodes_list]
    C = [node_colors[n] for n in nodes_list]
    
    # Marker sizes: Branch points smaller, leaves slightly larger
    nsizes = [30 if G.out_degree(n) == 0 else 10 for n in nodes_list]
    
    ax.scatter(T, R, s=nsizes, c=C, zorder=3, alpha=1.0, edgecolors='black', linewidth=0.3)
    
    depth_str = "Number of internal nodes from root (Topological Depth)." if use_topological else "Generation (Time) of the branching event or cell birth."
    textstr = (
        f"Circular Cell Phylogeny ({num_cells} Sampled Cells)\n\n"
        "• Each outer leaf dot represents a currently living individual cell.\n"
        "• Internal structure is a complete dendrogram where branching indicates cell splitting events.\n"
        f"• Distance from Center: {depth_str}"
    )
    props = dict(boxstyle='round', facecolor='white', alpha=0.9, edgecolor='gray')
    ax.text(0.05, 0.95, textstr, transform=fig.transFigure, fontsize=20,
            verticalalignment='top', bbox=props)
    
    plt.savefig(output_file, dpi=dpi, bbox_inches='tight', transparent=False, facecolor='white')
    plt.close()
    
    print(f"Cell phylogeny tree saved to: {output_file}")



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
    parser.add_argument('--cells', type=int, default=None, help='Generate tree for N randomly sampled cells from final population instead of clones (default for cell mode is 100 if none provided)')
    parser.add_argument('--linear_time', action='store_true', help='Use linear generation time for radial distance (default is topological depth for cells)')
    
    args = parser.parse_args()
    
    if not os.path.isdir(args.input):
        print(f"Error: Input directory does not exist: {args.input}")
        sys.exit(1)
        
    if args.cells is not None:
        if not args.output:
            suffix = "linear" if args.linear_time else "topological"
            args.output = os.path.join(args.input, "phylogeny", f"cell_tree_{args.cells}_{suffix}.png")
        plot_cell_phylogeny(args.input, args.cells, args.output, args.dpi, use_topological=not args.linear_time)
    else:
        if not args.output:
            args.output = os.path.join(args.input, "phylogeny", "clone_tree.png")
        plot_clone_phylogeny(args.input, args.output, args.dpi)

if __name__ == '__main__':
    main()
