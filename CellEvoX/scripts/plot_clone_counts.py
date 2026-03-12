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

def get_driver_mutation_type_ids(config_path):
    driver_ids = set()
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)
            for mutation in config.get('mutations', []):
                if mutation.get('is_driver', False):
                    driver_ids.add(mutation.get('id'))
    except Exception as e:
        print(f"Warning: Could not read config file {config_path}: {e}")
        # Default driver IDs if config parsing fails
        driver_ids = {1}
    return driver_ids

def get_generation_from_filename(filename):
    match = re.search(r'generation_(\d+)\.csv', filename)
    if match:
        return int(match.group(1))
    return -1


def plot_num_clones_over_time(input_dir, output_file, config_file):
    print(f"Generating clone counts chart from data in: {input_dir}")
    
    driver_type_ids = get_driver_mutation_type_ids(config_file)
    print(f"Using driver mutation IDs: {driver_type_ids}")
    
    # Grab all generational census files
    pop_dir = os.path.join(input_dir, "population_data")
    if not os.path.exists(pop_dir):
        print(f"Error: population_data directory not found in {input_dir}")
        return
        
    pop_files = glob.glob(os.path.join(pop_dir, "population_generation_*.csv"))
    if not pop_files:
        print(f"Error: No population files found in {pop_dir}")
        return
        
    pop_files.sort(key=lambda x: get_generation_from_filename(x))
    
    generations = []
    clone_counts = []
    
    for filename in pop_files:
        gen = get_generation_from_filename(filename)
        if gen < 0:
            continue
            
        try:
            df = pd.read_csv(filename)
            unique_clones = set()
            
            # Identify the unique distinct clones alive in this generation
            for _, row in df.iterrows():
                mutations_list = parse_mutations(row.get('Mutations', ''))
                signature = get_driver_signature(mutations_list, driver_type_ids)
                unique_clones.add(signature)
                
            num_clones = len(unique_clones)
            generations.append(gen)
            clone_counts.append(num_clones)
            
        except Exception as e:
            print(f"Error reading {filename}: {e}")
            
    if not generations:
        print("No valid generation data processed.")
        return
        
    # Convert vectors to pandas Series to compute sliding average
    s_gens = pd.Series(generations)
    s_counts = pd.Series(clone_counts)
    
    # Rolling (moving) average with a window of 5 checkpoints
    window_size = 5
    s_rolling_avg = s_counts.rolling(window=window_size, min_periods=1).mean()
    
    # Generate the Matplotlib Chart
    plt.figure(figsize=(10, 6), dpi=150)
    plt.plot(s_gens, s_counts, marker='.', linestyle='', color='lightblue', alpha=0.6, label='Raw Counts')
    plt.plot(s_gens, s_rolling_avg, linestyle='-', linewidth=2.5, color='darkblue', label=f'Moving Average (window={window_size})')
    
    plt.title('Number of Distinct Clones Over Time')
    plt.xlabel('Generation')
    plt.ylabel('Number of Unique Active Clones')
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    plt.tight_layout()
    
    # Save chart
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    plt.savefig(output_file)
    plt.close()
    print(f"Clone counts chart saved to: {output_file}")


def main():
    parser = argparse.ArgumentParser(description="Plot number of clones over generations.")
    parser.add_argument("--input", required=True, help="Directory containing the simulation run data")
    parser.add_argument("--output", help="Optional exact path for output image")
    
    args = parser.parse_args()
    
    input_dir = os.path.abspath(args.input)
    
    # Look for config file
    config_file = os.path.join(input_dir, "config.json")
    if not os.path.exists(config_file):
        config_file = os.path.join(os.path.dirname(input_dir), "config.json")
        
    if not args.output:
        args.output = os.path.join(input_dir, "clones", "clone_counts_over_time.png")
        
    plot_num_clones_over_time(input_dir, args.output, config_file)

if __name__ == "__main__":
    main()
