# Multi LLM-CNN Model Serving on Single-Core AI Accelerator

Korea University COSE461 Final Project  
Team ESCA

## Overview

This project implements a kernel-level wait-wake scheduling framework for concurrent AI model execution on a heterogeneous edge SoC integrating:

- CPU
- Saturn VPU
- Gemmini NPU

The system targets concurrent execution of:

- Large Language Models (LLMs)
- CNN inference workloads

on a single-core accelerator environment.

Unlike conventional blocking execution using `gemmini_fence()`, the proposed framework introduces:

- interrupt-driven synchronization
- wait-wake scheduling
- chunk-based RoCC dispatching
- deadline-aware scheduling
- bounded-streak fairness

to improve accelerator utilization and reduce LLM serving latency.

---

## Key Features

### Wait-Wake Execution Model

Model processes voluntarily sleep while accelerator operations remain in flight.

The kernel scheduler wakes runnable workloads through interrupt-driven completion handling.

This removes unnecessary CPU blocking during accelerator execution.

### Chunk-Based RoCC Dispatch

RoCC instruction streams are partitioned into fixed-size chunks to:

- reduce synchronization overhead
- avoid Gemmini queue back-pressure
- prevent command stream corruption

### Deadline-Aware Scheduling

The scheduler prioritizes urgent workloads using slack-based scheduling while preventing starvation through bounded-streak fairness.

### Concurrent Multi-Model Execution

The framework supports concurrent execution of:

- BitNet-based LLM inference
- ResNet50
- MobileNet

under shared accelerator resources.

---

## Experimental Environment

- Chipyard
- Gemmini
- FireSim
- RISC-V Linux
- Saturn VPU

---

## Example Execution

```bash
./multimodel_infinite.riscv \
  -s 1 15 \
  -d 2055 885 81350 \
  -p 2500 1000 81350