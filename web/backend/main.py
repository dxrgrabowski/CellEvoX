"""
CellEvoX Web Backend — FastAPI bridge
Ports: API=7432, WebSocket on same server
"""
import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from runner import SimulationRunner
from results_parser import ResultsParser

# Add CellEvoX scripts to path for importing plot utilities
REPO_ROOT = Path(__file__).parent.parent.parent
SCRIPTS_DIR = REPO_ROOT / "CellEvoX" / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

app = FastAPI(title="CellEvoX API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5274", "http://127.0.0.1:5274"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

runner = SimulationRunner(repo_root=REPO_ROOT)
results_parser = ResultsParser(repo_root=REPO_ROOT)


# ── Config Schema ─────────────────────────────────────────────────────────────

@app.get("/api/config/schema")
async def get_config_schema():
    """Return JSON schema for SimulationConfig"""
    return {
        "core": {
            "seed": {"type": "integer", "default": 42, "min": 0, "max": 2**31},
            "stochastic": {"type": "boolean", "default": True},
            "simulation_mode": {
                "type": "enum",
                "values": ["stochastic", "deterministic", "spatial_3d_density", "spatial_3d_capacity"],
                "default": "stochastic"
            },
            "tau_step": {"type": "float", "default": 0.005, "min": 0.0001, "max": 1.0, "step": 0.0001},
        },
        "population": {
            "initial_population": {"type": "integer", "default": 1000, "min": 1, "max": 10_000_000},
            "env_capacity": {"type": "integer", "default": 10000, "min": 1, "max": 10_000_000},
            "steps": {"type": "integer", "default": 100000, "min": 1000, "max": 100_000_000},
            "max_population_cutoff": {"type": "integer", "default": 0, "min": 0, "max": 10_000_000, "hint": "0 = disabled"},
        },
        "output": {
            "output_path": {"type": "string", "default": "./output"},
            "statistics_resolution": {"type": "integer", "default": 10, "min": 1},
            "population_statistics_res": {"type": "integer", "default": 500, "min": 1},
            "graveyard_pruning_interval": {"type": "integer", "default": 500, "min": 0},
            "full_mutation_payload": {"type": "boolean", "default": False},
            "verbosity": {"type": "enum", "values": [0, 1, 2], "labels": ["Off", "Minimal", "Full"], "default": 1},
            "phylogeny_num_cells_sampling": {"type": "integer", "default": 100, "min": 10, "max": 10000},
        },
        "spatial": {
            "spatial_domain_size": {"type": "float", "default": 200.0, "min": 10.0, "max": 2000.0},
            "max_local_density": {"type": "float", "default": 8.0, "min": 1.0, "max": 100.0},
            "sample_radius": {"type": "float", "default": 3.0, "min": 0.5, "max": 50.0},
            "spring_constant": {"type": "float", "default": 0.5, "min": 0.01, "max": 10.0},
            "mech_dt": {"type": "float", "default": 0.1, "min": 0.001, "max": 1.0},
            "mech_substeps": {"type": "integer", "default": 5, "min": 1, "max": 100},
            "epsilon": {"type": "float", "default": 0.1, "min": 0.001, "max": 1.0},
        },
        "mutation_fields": {
            "id": {"type": "integer"},
            "is_driver": {"type": "boolean"},
            "effect": {"type": "float", "min": -0.5, "max": 0.5, "step": 0.001},
            "probability": {"type": "float", "min": 0.0001, "max": 0.5, "step": 0.0001},
        }
    }


# ── Simulation Control ────────────────────────────────────────────────────────

class SimulationStartRequest(BaseModel):
    config: dict


@app.post("/api/simulation/start")
async def start_simulation(req: SimulationStartRequest):
    if runner.is_running():
        raise HTTPException(status_code=409, detail="Simulation already running")
    try:
        run_id = await runner.start(req.config)
        return {"status": "started", "run_id": run_id}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/api/simulation/stop")
async def stop_simulation():
    runner.stop()
    return {"status": "stopped"}


@app.get("/api/simulation/status")
async def simulation_status():
    return runner.get_status()


# ── WebSocket Log Stream ──────────────────────────────────────────────────────

@app.websocket("/ws/logs")
async def websocket_logs(websocket: WebSocket):
    await websocket.accept()
    try:
        async for line in runner.log_stream():
            await websocket.send_text(json.dumps({"type": "log", "data": line}))
        await websocket.send_text(json.dumps({"type": "done"}))
    except WebSocketDisconnect:
        pass


# ── Results ───────────────────────────────────────────────────────────────────

@app.get("/api/results")
async def list_results():
    """List all available simulation run directories"""
    return results_parser.list_runs()


@app.get("/api/results/{run_id}/config")
async def get_run_config(run_id: str):
    config = results_parser.get_config(run_id)
    if config is None:
        raise HTTPException(status_code=404, detail="Run not found")
    return config


@app.get("/api/results/{run_id}/summary")
async def get_run_summary(run_id: str):
    summary = results_parser.get_summary(run_id)
    if summary is None:
        raise HTTPException(status_code=404, detail="Run not found")
    return summary


@app.get("/api/results/{run_id}/stats")
async def get_run_stats(run_id: str):
    data = results_parser.get_stats(run_id)
    if data is None:
        raise HTTPException(status_code=404, detail="Stats not found")
    return data


@app.get("/api/results/{run_id}/muller")
async def get_muller_data(run_id: str):
    """Return processed Müller plot data as JSON for Plotly.js rendering"""
    data = results_parser.get_muller_data(run_id)
    if data is None:
        raise HTTPException(status_code=404, detail="Population data not found")
    return data


@app.get("/api/results/{run_id}/population")
async def get_population_data(run_id: str, generation: Optional[int] = None):
    data = results_parser.get_population(run_id, generation)
    if data is None:
        raise HTTPException(status_code=404, detail="Population data not found")
    return data


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=7432, reload=True)
