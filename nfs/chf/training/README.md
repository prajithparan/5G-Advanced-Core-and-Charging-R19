# CHF predictive quota sizing -- training sidecar

P4.8 (`CHARGING_PROMPT.md` Angle 1a, ADR-0074). Trains the model CHF's own in-process
`AiQuotaSizer` (`nfs/chf/src/ai_inference.hpp/.cpp`) loads at startup. This directory is the
*only* place training happens -- CLAUDE.md's mandated pattern: training is Python, offline;
inference is in-process C++, at runtime, never a Python call.

## What it predicts

Real regression target: how much a given SUPI+ratingGroup will actually consume
(`used_total_volume`, octets) in its next charging-request window, learned from real CDR history
already written to Apache Doris (ADR-0192; `nfs/chf/schema.doris.sql`).
It does **not** predict a "correct multiplier" -- no such label exists in this project's real
data. CHF's own deterministic rule
(`build_rating_grant`, `charging_engine.cpp`) turns a predicted usage figure into a bounded
grant-size multiplier (clamped to `[0.5x, 2.0x]` of the price-configured base grant). See
`train_quota_sizing.py`'s own module docstring for the full, real feature-vector definition.

## Cold start

A fresh lab environment has no real production usage history. If Doris doesn't yet have at
least 20 real usage-bearing CDR rows for a SUPI+ratingGroup sequence, the script falls back to a
clearly-labeled SYNTHETIC bootstrap dataset (a simple, documented generative rule with noise) so
the training->ONNX->inference pipeline is provably real and functional before real usage data
exists. The data source actually used is always logged (MLflow tag `data_source`, and printed to
stdout) -- never silently blended or hidden. Re-run this script once real CDR volume accumulates.

## Usage

```
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python train_quota_sizing.py \
    --doris-host 127.0.0.1 --doris-user root \
    --doris-password <CHF_DORIS_PASSWORD> \
    --output models/quota_sizing.onnx
```

Writes `models/quota_sizing.onnx` (point `CHF_QUOTA_MODEL_PATH` at this) and
`models/quota_sizing.onnx.version` (the MLflow run id, read by `AiQuotaSizer` for governance
logging into `rating_decision.ai_advisory`). MLflow tracking defaults to a local SQLite file
(`mlflow.db`) -- real, disclosed finding from live testing: MLflow 3.x's plain filesystem tracking
store (`file://./mlruns`) is now in maintenance mode and refuses new writes.

Then set `CHF_AI_QUOTA_SIZING_ENABLED=true` and `CHF_QUOTA_MODEL_PATH=<path>` on CHF -- the real
kill switch, off by default (see `deploy/docker/docker-compose.yml`'s own comment).
