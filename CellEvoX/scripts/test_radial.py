import argparse
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import networkx as nx

from plot_phylogeny import build_clone_tree

def plot_circular_dendrogram(G: nx.DiGraph, output_file: str, dpi: int = 300):
    if len(G.nodes) == 0:
        print("Empty graph")
        return
        
    roots = [n for n, d in G.in_degree() if d == 0]
    if not roots:
        print("No roots")
        return
    root = roots[0]
    
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
    
    fig, ax = plt.subplots(figsize=(16, 16), subplot_kw=dict(polar=True))
    ax.set_axis_off()
    
    import matplotlib.cm as cm
    cmap = cm.get_cmap('tab20')
    main_branches = list(G.successors(root))
    branch_colors = {}
    for i, b in enumerate(main_branches):
        branch_colors[b] = cmap(i % 20)
        
    node_colors = {root: 'black'}
    def assign_colors(node, current_color):
        for child in G.successors(node):
            c = branch_colors.get(child, current_color)
            node_colors[child] = c
            assign_colors(child, c)
            
    assign_colors(root, 'black')
    
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
        ax.plot(theta_vals, r_vals, color=c, linewidth=1.5, alpha=0.8)
        ax.plot([tv, tv], [r_u, r_v], color=c, linewidth=1.5, alpha=0.8)
        
    sizes = [G.nodes[n].get('size', 0) for n in G.nodes]
    if sizes:
        max_size = max(sizes)
        nsizes = [5 + 150 * (np.log(s + 1) / np.log(max_size + 2)) for s in sizes]
    else:
        nsizes = [20] * len(G.nodes)
        
    nodes_list = list(G.nodes)
    T = [thetas[n] for n in nodes_list]
    R = [depths[n] for n in nodes_list]
    C = [node_colors[n] for n in nodes_list]
    
    ax.scatter(T, R, s=nsizes, c=C, zorder=3, alpha=1.0, edgecolors='white', linewidth=0.5)
    
    plt.savefig(output_file, dpi=dpi, bbox_inches='tight', transparent=True)
    plt.close()
    print(f"Saved to {output_file}")

if __name__ == '__main__':
    G = build_clone_tree("/home/dwg/rep/CellEvoX/CellEvoX/build/output/2026-03-11_04-59-31")
    plot_circular_dendrogram(G, "test_radial.png")
