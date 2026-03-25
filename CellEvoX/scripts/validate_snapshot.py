#!/usr/bin/env python3
"""
Validate CellEvoX 3D Off-Lattice ABM binary snapshots.

Reads .bin population files produced by SimulationEngine::writeBinarySnapshot(),
validates the data integrity, and prints summary statistics.

Binary format:
  - 4 bytes: uint32_t cell_count
  - cell_count × 25 bytes: CellSnapshotBinary structs
    Each struct: id(u32) parent_id(u32) fitness(f32) x(f32) y(f32) z(f32) mutations_count(u8)
"""

import struct
import sys
import os
import math
import glob

SNAPSHOT_FORMAT = '<IIfffffB'  # little-endian: 2 uint32, 4 float, 1 uint8 → 25 bytes
SNAPSHOT_SIZE = struct.calcsize(SNAPSHOT_FORMAT)

def read_snapshot(filepath):
    """Read a binary snapshot file, return list of cell dicts."""
    cells = []
    with open(filepath, 'rb') as f:
        header = f.read(4)
        if len(header) < 4:
            print(f"  ERROR: file too small to read header")
            return cells
        cell_count = struct.unpack('<I', header)[0]
        
        for i in range(cell_count):
            data = f.read(SNAPSHOT_SIZE)
            if len(data) < SNAPSHOT_SIZE:
                print(f"  ERROR: truncated at cell {i}/{cell_count}")
                break
            fields = struct.unpack(SNAPSHOT_FORMAT, data)
            cells.append({
                'id': fields[0],
                'parent_id': fields[1],
                'fitness': fields[2],
                'x': fields[3],
                'y': fields[4],
                'z': fields[5],
                'mutations_count': fields[6]
            })
    return cells

def validate_snapshot(filepath):
    """Validate a single snapshot file. Return (ok, cell_count, stats)."""
    cells = read_snapshot(filepath)
    if not cells:
        return True, 0, {}  # Empty is valid (population extinction)
    
    errors = []
    
    # Check all positions are finite
    for c in cells:
        if not (math.isfinite(c['x']) and math.isfinite(c['y']) and math.isfinite(c['z'])):
            errors.append(f"  Cell {c['id']}: non-finite position ({c['x']}, {c['y']}, {c['z']})")
        if not math.isfinite(c['fitness']):
            errors.append(f"  Cell {c['id']}: non-finite fitness {c['fitness']}")
        if c['fitness'] <= 0:
            errors.append(f"  Cell {c['id']}: non-positive fitness {c['fitness']}")
    
    # Check unique IDs
    ids = [c['id'] for c in cells]
    if len(ids) != len(set(ids)):
        errors.append(f"  Duplicate IDs found!")
    
    # Compute stats
    fitnesses = [c['fitness'] for c in cells]
    positions = [(c['x'], c['y'], c['z']) for c in cells]
    distances_from_origin = [math.sqrt(x**2 + y**2 + z**2) for x,y,z in positions]
    mutations = [c['mutations_count'] for c in cells]
    
    stats = {
        'cell_count': len(cells),
        'mean_fitness': sum(fitnesses) / len(fitnesses),
        'min_fitness': min(fitnesses),
        'max_fitness': max(fitnesses),
        'mean_distance_from_origin': sum(distances_from_origin) / len(distances_from_origin),
        'max_distance_from_origin': max(distances_from_origin),
        'mean_mutations': sum(mutations) / len(mutations),
        'max_mutations': max(mutations),
    }
    
    # Compute min pairwise distance (for small populations)
    if len(cells) <= 500:
        min_pair_dist = float('inf')
        for i in range(len(cells)):
            for j in range(i+1, len(cells)):
                dx = cells[i]['x'] - cells[j]['x']
                dy = cells[i]['y'] - cells[j]['y']
                dz = cells[i]['z'] - cells[j]['z']
                d = math.sqrt(dx*dx + dy*dy + dz*dz)
                if d < min_pair_dist:
                    min_pair_dist = d
        stats['min_pairwise_distance'] = min_pair_dist
    
    if errors:
        for e in errors:
            print(e)
        return False, len(cells), stats
    
    return True, len(cells), stats

def main():
    if len(sys.argv) < 2:
        print("Usage: python validate_snapshot.py <directory_with_bin_files>")
        print("       python validate_snapshot.py <single_file.bin>")
        sys.exit(1)
    
    path = sys.argv[1]
    
    if os.path.isfile(path):
        files = [path]
    elif os.path.isdir(path):
        files = sorted(glob.glob(os.path.join(path, "*.bin")))
    else:
        print(f"Path not found: {path}")
        sys.exit(1)
    
    if not files:
        print("No .bin files found!")
        sys.exit(1)
    
    print(f"{'='*70}")
    print(f"CellEvoX 3D ABM Binary Snapshot Validator")
    print(f"{'='*70}")
    
    all_ok = True
    for filepath in files:
        basename = os.path.basename(filepath)
        ok, count, stats = validate_snapshot(filepath)
        
        status = "✓ PASS" if ok else "✗ FAIL"
        print(f"\n{status}  {basename}")
        print(f"  Cells: {count}")
        if stats:
            print(f"  Fitness:  mean={stats['mean_fitness']:.4f}  "
                  f"min={stats['min_fitness']:.4f}  max={stats['max_fitness']:.4f}")
            print(f"  Position: mean_dist={stats['mean_distance_from_origin']:.2f}  "
                  f"max_dist={stats['max_distance_from_origin']:.2f}")
            print(f"  Mutations: mean={stats['mean_mutations']:.2f}  "
                  f"max={stats['max_mutations']}")
            if 'min_pairwise_distance' in stats:
                d = stats['min_pairwise_distance']
                overlap = "YES (< 2R)" if d < 2.0 else "NO"
                print(f"  Min pairwise distance: {d:.4f}  Overlapping: {overlap}")
        
        if not ok:
            all_ok = False
    
    print(f"\n{'='*70}")
    if all_ok:
        print("ALL SNAPSHOTS VALID ✓")
    else:
        print("SOME SNAPSHOTS HAVE ERRORS ✗")
    print(f"{'='*70}")
    
    return 0 if all_ok else 1

if __name__ == '__main__':
    sys.exit(main())
