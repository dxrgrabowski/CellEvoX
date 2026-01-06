#!/usr/bin/env python3
"""
Generate synthetic test data for Müller plot validation.

Creates realistic evolutionary scenarios:
1. Ancestral clone starts at 100%
2. Driver mutations create new clones that grow
3. Parent clones shrink as children emerge
4. Some clones go extinct
5. Multiple children can emerge from same parent at different times
"""

import json
import os
import pandas as pd
import random

def generate_test_data(output_dir: str, num_generations: int = 1000):
    """
    Generate test data with specific evolutionary scenarios.
    """
    os.makedirs(output_dir, exist_ok=True)
    
    # Create config.json with driver mutation
    config = {
        "mutations": [
            {"id": 1, "is_driver": True, "effect": 0.05, "probability": 0.001}
        ]
    }
    
    with open(os.path.join(output_dir, 'config.json'), 'w') as f:
        json.dump(config, f, indent=2)
    
    # Define evolutionary scenario
    # Clone signatures: mutation IDs that define each clone
    # Timeline: when clones emerge and their population dynamics
    
    scenarios = {
        # Ancestor: no mutations
        'ancestor': {
            'mutations': '',
            'birth_gen': 0,
            'death_gen': None,  # Still alive
            'peak_gen': 50,
            'peak_fraction': 1.0
        },
        # First driver mutation emerges at gen 100
        '1001': {
            'mutations': '(1001,1)',
            'birth_gen': 100,
            'death_gen': None,
            'peak_gen': 300,
            'peak_fraction': 0.8
        },
        # Second independent driver at gen 150
        '2002': {
            'mutations': '(2002,1)',
            'birth_gen': 150,
            'death_gen': 400,  # Goes extinct
            'peak_gen': 250,
            'peak_fraction': 0.3
        },
        # Child of first clone emerges at gen 300
        '1001,3003': {
            'mutations': '(1001,1) (3003,1)',
            'birth_gen': 300,
            'death_gen': None,
            'peak_gen': 600,
            'peak_fraction': 0.9
        },
        # Another child of first clone at gen 350
        '1001,4004': {
            'mutations': '(1001,1) (4004,1)',
            'birth_gen': 350,
            'death_gen': 700,  # Goes extinct
            'peak_gen': 500,
            'peak_fraction': 0.4
        },
        # Grandchild emerges at gen 600
        '1001,3003,5005': {
            'mutations': '(1001,1) (3003,1) (5005,1)',
            'birth_gen': 600,
            'death_gen': None,
            'peak_gen': 900,
            'peak_fraction': 0.95
        }
    }
    
    # Generate population data for each generation
    generation_points = list(range(0, num_generations + 1, 25))  # Every 25 generations
    
    for gen in generation_points:
        cells = []
        
        # Calculate population for each clone at this generation
        clone_populations = {}
        
        for clone_sig, scenario in scenarios.items():
            if gen < scenario['birth_gen']:
                pop = 0
            elif scenario['death_gen'] and gen > scenario['death_gen']:
                pop = 0
            else:
                # Logistic growth from birth to peak, then decline if has death
                if gen <= scenario['peak_gen']:
                    # Growth phase
                    progress = (gen - scenario['birth_gen']) / (scenario['peak_gen'] - scenario['birth_gen'])
                    pop = scenario['peak_fraction'] * (1 / (1 + 9 * (1 - progress)))
                else:
                    if scenario['death_gen']:
                        # Decline phase
                        decline_progress = (gen - scenario['peak_gen']) / (scenario['death_gen'] - scenario['peak_gen'])
                        pop = scenario['peak_fraction'] * (1 - decline_progress)
                    else:
                        # Maintain or slight decline
                        pop = scenario['peak_fraction'] * 0.95
            
            clone_populations[clone_sig] = max(0, pop)
        
        # Normalize to sum to 1.0 (100%)
        total = sum(clone_populations.values())
        if total > 0:
            clone_populations = {k: v/total for k, v in clone_populations.items()}
        
        # Generate individual cells (10,000 total)
        total_cells = 10000
        cell_id = gen * 100000  # Unique IDs per generation
        
        for clone_sig, fraction in clone_populations.items():
            num_cells = int(fraction * total_cells)
            for _ in range(num_cells):
                cells.append({
                    'CellID': cell_id,
                    'ParentID': cell_id - 1 if cell_id > 0 else 0,
                    'Fitness': 1.0 + random.uniform(-0.01, 0.01),
                    'Mutations': scenarios[clone_sig]['mutations']
                })
                cell_id += 1
        
        # Save to CSV
        df = pd.DataFrame(cells)
        output_file = os.path.join(output_dir, f'population_generation_{gen}.csv')
        df.to_csv(output_file, index=False)
        print(f"Generated {output_file} with {len(cells)} cells")
    
    print(f"\nTest data generated in: {output_dir}")
    print(f"Generations: {len(generation_points)}")
    print(f"Clones: {len(scenarios)}")
    print("\nScenarios:")
    for clone, info in scenarios.items():
        status = "extinct" if info['death_gen'] else "alive"
        print(f"  {clone}: birth={info['birth_gen']}, peak={info['peak_gen']}, {status}")

if __name__ == '__main__':
    output_dir = '/tmp/muller_test_data'
    generate_test_data(output_dir, num_generations=1000)
    print(f"\n✅ Test data ready!")
    print(f"\nTo generate plot, run:")
    print(f"  /workspaces/CellEvoX/.venv/bin/python /workspaces/CellEvoX/CellEvoX/scripts/plot_muller.py \\")
    print(f"    --input {output_dir} \\")
    print(f"    --output {output_dir}/muller_test_plot.png")
