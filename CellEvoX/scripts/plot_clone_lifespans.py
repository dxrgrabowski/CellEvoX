#!/usr/bin/env python3
import argparse
import glob
import json
import os
import re
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

def parse_mutations(mutations_str):
    if not mutations_str or pd.isna(mutations_str) or mutations_str == '""':
        return []
    mutations_str = str(mutations_str).strip('"')
    if not mutations_str:
        return []
    mutations = []
    pattern = r'\((\d+),(\d+)\)'
    for match in re.finditer(pattern, mutations_str):
        mutations.append((int(match.group(1)), int(match.group(2))))
    return mutations

def get_driver_signature(mutations_list, driver_type_ids):
    drivers = [mut_id for mut_id, mut_type in mutations_list if mut_type in driver_type_ids]
    return tuple(sorted(drivers))

def get_config_options(config_path):
    driver_ids = set()
    pop_res = 50
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
            for mutation in config.get('mutations', []):
                if mutation.get('is_driver', False):
                    driver_ids.add(mutation.get('id'))
            pop_res = config.get('population_statistics_res', 50)
    except Exception as e:
        print(f"Warning: Could not read config file {config_path}: {e}")
        driver_ids = {1}
    return driver_ids, pop_res

def get_generation_from_filename(filename):
    match = re.search(r'generation_(\d+)\.csv', filename)
    if match:
        return int(match.group(1))
    return -1

def plot_clone_lifespans(input_dir, output_file, config_file):
    print(f"Generating clone lifespans chart from data in: {input_dir}")
    
    driver_type_ids, pop_res = get_config_options(config_file)
    print(f"Using driver mutation IDs: {driver_type_ids}, Resolution: {pop_res}")
    
    pop_dir = os.path.join(input_dir, "population_data")
    if not os.path.exists(pop_dir):
        print(f"Error: population_data directory not found in {input_dir}")
        return
        
    pop_files = glob.glob(os.path.join(pop_dir, "population_generation_*.csv"))
    if not pop_files:
        print(f"Error: No population files found in {pop_dir}")
        return
        
    pop_files.sort(key=lambda x: get_generation_from_filename(x))
    
    # Store the first and last time a clone signature was seen
    # Dictionary mapping signature -> {"first": gen_num, "last": gen_num}
    clone_lifespans = {}
    
    last_generation_seen = -1
    
    for filename in pop_files:
        gen = get_generation_from_filename(filename)
        if gen < 0:
            continue
            
        last_generation_seen = max(last_generation_seen, gen)
            
        try:
            df = pd.read_csv(filename)
            unique_clones_in_gen = set()
            
            for _, row in df.iterrows():
                mutations_list = parse_mutations(row.get('Mutations', ''))
                signature = get_driver_signature(mutations_list, driver_type_ids)
                unique_clones_in_gen.add(signature)
                
            for signature in unique_clones_in_gen:
                if signature not in clone_lifespans:
                    clone_lifespans[signature] = {"first": gen, "last": gen}
                else:
                    clone_lifespans[signature]["last"] = gen
                    
        except Exception as e:
            print(f"Error reading {filename}: {e}")
            
    if not clone_lifespans:
        print("No valid clone data processed.")
        return
        
    # Calculate lifespans
    # If a clone survives until the very last snapshot, its lifespan is truncated.
    # To get a statistically clean view, we only plot clones that WENT EXTINCT 
    # (i.e., their last seen generation is < last_generation_seen)
    # FURTHERMORE, based on `population_statistics_res`, if a clone's calculated 
    # lifespan is < pop_res, it means it only lived in exactly 1 snapshot file and
    # immediately died. We filter these out as "measurement noise" to clarify the plot.
    
    extinct_lifespans_all = []
    extinct_lifespans_filtered = []
    
    for signature, timing in clone_lifespans.items():
        if timing["last"] < last_generation_seen:
            # The clone went extinct. 
            lifespan = timing["last"] - timing["first"]
            extinct_lifespans_all.append(lifespan)
            if lifespan >= pop_res:
                extinct_lifespans_filtered.append(lifespan)
            
    print(f"Calculated lifespans for {len(extinct_lifespans_all)} total extinct clones.")
    print(f"From those, {len(extinct_lifespans_filtered)} extinct clones survived >= {pop_res} generations.")
            
    def save_histogram_plot(data, filename, title):
        if not data:
            print(f"Not enough data to plot for {filename}")
            return
            
        plt.figure(figsize=(10, 6), dpi=150)
        
        # Determine bins: max 50 bins, ensuring bin width is at least pop_res
        max_val = max(data)
        bin_width = max(pop_res, int(max_val / 50))
        if bin_width == 0:
             bin_width = 1
        bins = range(0, max_val + bin_width, bin_width)
        
        counts, bins, patches = plt.hist(data, bins=bins, color='lightcoral', edgecolor='darkred', alpha=0.7)
        
        # Add exact numbers on top of bars
        for i, count in enumerate(counts):
            if count > 0:
                plt.text(bins[i] + bin_width/2, count, str(int(count)), ha='center', va='bottom', fontsize=8, color='black')
        
        mean_val = np.mean(data)
        median_val = np.median(data)
        
        plt.axvline(mean_val, color='blue', linestyle='dashed', linewidth=2, label=f'Mean: {mean_val:.1f}')
        plt.axvline(median_val, color='indigo', linestyle='dashed', linewidth=2, label=f'Median: {median_val:.1f}')
        
        plt.title(title)
        plt.xlabel('Lifespan (Generations lived)')
        plt.ylabel('Number of Clones')
        plt.legend(loc='upper right')
        
        plt.grid(True, axis='y', linestyle='--', alpha=0.7)
        plt.tight_layout()
        
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        plt.savefig(filename)
        plt.close()
        print(f"Clone lifespans histogram plot saved to: {filename}")

    def save_violin_plot(data, filename, title):
        if not data:
            print(f"Not enough data to plot for {filename}")
            return
            
        plt.figure(figsize=(10, 6), dpi=150)
        parts = plt.violinplot([data], showmeans=True, showmedians=True, vert=False)
        
        for pc in parts['bodies']:
            pc.set_facecolor('lightcoral')
            pc.set_edgecolor('darkred')
            pc.set_alpha(0.7)
            
        parts['cmeans'].set_color('blue')
        parts['cmedians'].set_color('indigo')
        parts['cbars'].set_color('black')
        parts['cmins'].set_color('black')
        parts['cmaxes'].set_color('black')
        
        plt.title(title)
        plt.xlabel('Lifespan (Generations lived)')
        plt.yticks([1], ['Clones'])
        import matplotlib.patches as mpatches
        mean_patch = mpatches.Patch(color='blue', label='Mean')
        median_patch = mpatches.Patch(color='indigo', label='Median')
        plt.legend(handles=[mean_patch, median_patch], loc='upper right')
        
        plt.grid(True, axis='x', linestyle='--', alpha=0.7)
        plt.tight_layout()
        
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        plt.savefig(filename)
        plt.close()
        print(f"Clone lifespans violin plot saved to: {filename}")

    if output_file.endswith('.png'):
        base_name = output_file.replace('.png', '')
        save_histogram_plot(extinct_lifespans_all, base_name + '_unfiltered.png', 'Extinct Clone Lifespans (All Clones)')
        save_histogram_plot(extinct_lifespans_filtered, base_name + '_filtered.png', f'Extinct Clone Lifespans (Filtered >= {pop_res} gen)')
        
        save_violin_plot(extinct_lifespans_all, base_name.replace('_histogram', '') + '_violin_unfiltered.png', 'Extinct Clone Lifespan Distribution (All Clones)')
        save_violin_plot(extinct_lifespans_filtered, base_name.replace('_histogram', '') + '_violin_filtered.png', f'Extinct Clone Lifespan Distribution (Filtered >= {pop_res} gen)')
    else:
        save_histogram_plot(extinct_lifespans_all, output_file + '_unfiltered.png', 'Extinct Clone Lifespans (All Clones)')
        save_histogram_plot(extinct_lifespans_filtered, output_file + '_filtered.png', f'Extinct Clone Lifespans (Filtered >= {pop_res} gen)')
        
        save_violin_plot(extinct_lifespans_all, output_file.replace('_histogram', '') + '_violin_unfiltered.png', 'Extinct Clone Lifespan Distribution (All Clones)')
        save_violin_plot(extinct_lifespans_filtered, output_file.replace('_histogram', '') + '_violin_filtered.png', f'Extinct Clone Lifespan Distribution (Filtered >= {pop_res} gen)')


def main():
    parser = argparse.ArgumentParser(description="Plot clone lifespans using a violin plot.")
    parser.add_argument("--input", required=True, help="Directory containing the simulation run data")
    parser.add_argument("--output", help="Optional exact path for output image")
    
    args = parser.parse_args()
    
    input_dir = os.path.abspath(args.input)
    config_file = os.path.join(input_dir, "config.json")
    if not os.path.exists(config_file):
        config_file = os.path.join(os.path.dirname(input_dir), "config.json")
        
    if not args.output:
        args.output = os.path.join(input_dir, "clones", "clone_lifespan_histogram.png")
        
    plot_clone_lifespans(input_dir, args.output, config_file)

if __name__ == "__main__":
    main()
