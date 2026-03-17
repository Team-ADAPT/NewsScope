# NewsScope

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)

A deterministic, high-performance news credibility assessment engine written in C++17. NewsScope evaluates articles through a pipeline of independent algorithmic modules — no machine learning — and returns a scored, fully explainable credibility result per article.

```
NewsScope: Scalable News Credibility Assessment System

Article: article_1
Overall Credibility Score: 82.50/100

Module Scores:
  source_validation         : 95.00/100
  frequency_analysis        : 78.00/100
  greedy_filter             : 80.00/100
  ...

Processing Time: 0 ms
```

---

## Table of Contents

- [Overview](#overview)
- [Modules](#modules)
- [Architecture](#architecture)
- [Scoring](#scoring)
- [Getting Started](#getting-started)
- [Usage](#usage)
- [Benchmarks](#benchmarks)
- [Project Structure](#project-structure)
- [Documentation](#documentation)

---

## Overview

NewsScope is built for use cases where **explainability**, **determinism**, and **performance** are non-negotiable. Each article passes through a pipeline of independent modules. Every module contributes a numeric score and a human-readable explanation. The final score is a configurable weighted average across all modules.

**Key properties:**

- Pure algorithmic scoring — no model training, no external ML dependencies
- Modular architecture with clean, independently testable interfaces
- Concurrent batch processing via a custom thread pool
- Full benchmark suite covering throughput, latency, and memory usage
- Builds with a single `make` command — no package manager required

---

## Modules

| Module | Algorithm | Time Complexity | Role |
|---|---|---|---|
| Preprocessing | Tokenization + normalization + stop-word removal | `O(n)` | Text cleanup pipeline |
| Source Validation | `unordered_map` lookup | `O(1)` avg | Trusted/untrusted source scoring |
| Phrase Indexing | Trie | `O(m)` per insert/search | Suspicious phrase detection |
| KMP Matcher | Prefix/LPS-based pattern matching | `O(n + m)` | Deterministic single-pattern match |
| Rabin-Karp Matcher | Rolling hash | `O(n + m)` avg | Multi-pattern matching |
| Frequency Analyzer | Hash-map term counting | `O(n)` | Suspicious term frequency scoring |
| Temporal Analyzer | Sliding window (`deque`) | Amortized `O(1)` | Publish-rate spike detection |
| Greedy Filter | Priority-driven signal selection | `O(k log k)` | Clickbait/manipulation prioritization |
| Scoring Engine | Weighted aggregation | `O(1)` | Combines all module outputs |

`n` = text length, `m` = pattern length, `k` = detected signal count.

---

## Architecture

```
Article(id, headline, body, source, timestamp)
        │
        ▼
  Preprocessor
        │
        ├──▶ SourceValidator    (hash lookup)
        ├──▶ PhraseIndexer      (trie scan)
        ├──▶ StringMatcher      (KMP + Rabin-Karp)
        ├──▶ FrequencyAnalyzer  (term frequency)
        ├──▶ TemporalAnalyzer   (sliding window)
        └──▶ GreedyFilter       (signal prioritization)
                    │
                    ▼
             ScoringEngine
                    │
                    ▼
  CredibilityResult(overall_score, module_scores, explanations, processing_time)
```

`ScoringEngine` orchestrates all modules and exposes both single-article and batch assessment APIs. `ThreadPool` enables concurrent processing for high-throughput workloads using a task queue backed by condition variables and mutex-protected worker threads.

---

## Scoring

All eight modules contribute equally by default (`12.5%` each). Weights are fully configurable at runtime:

```cpp
engine.set_module_weights(
    preprocessing, source, phrase, kmp,
    rabin_karp, frequency, temporal, greedy
);
```

Score interpretation:

| Range | Credibility Level |
|---|---|
| 90 – 100 | **Very High** — trusted source, factual content |
| 70 – 89 | **High** — generally reliable with minor concerns |
| 50 – 69 | **Medium** — mixed signals, verify independently |
| 30 – 49 | **Low** — multiple red flags detected |
| 0 – 29 | **Very Low** — high likelihood of misinformation |

---

## Getting Started

**Prerequisites:** C++17 compiler (`clang++` or `g++`), `make`

```bash
# Clone the repository
git clone https://github.com/<your-username>/NewsScope.git
cd NewsScope

# Build everything (library + demo + tests + benchmarks)
make all

# Run the demo
./build/newsscope_demo

# Run all unit tests
make run-tests

# Run all benchmarks
make run-benchmarks
```

Individual benchmark targets:

```bash
make run-throughput   # Articles/sec throughput
make run-latency      # P50/P99 latency distribution
make run-memory       # Heap usage per batch size
```

CMake is also supported:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Clean build artifacts:

```bash
make clean
```

---

## Usage

**Single article assessment:**

```cpp
#include "scoring_engine.h"
using namespace newsscope;

ScoringEngine engine;
engine.initialize("data/sources.csv", "data/suspicious_phrases.txt", "");

Article article(
    "article-1",
    "New Research Reveals Cancer Treatment Breakthrough",
    "Researchers at Stanford University have announced a significant breakthrough...",
    "Reuters"
);

CredibilityResult result = engine.assess_article(article);
// result.overall_score    → 0–100
// result.module_scores    → per-module score breakdown
// result.explanations     → human-readable reasoning per module
// result.processing_time  → end-to-end latency in ms
```

**Batch processing:**

```cpp
std::vector<Article> articles = { ... };
std::vector<CredibilityResult> results = engine.assess_batch(articles);
```

**Concurrent processing with thread pool:**

```cpp
ThreadPool pool(std::thread::hardware_concurrency());

for (const auto& article : articles) {
    pool.enqueue([&engine, &article]() {
        auto result = engine.assess_article(article);
    });
}

pool.wait_for_all();
```

**Custom module weights:**

```cpp
// Emphasize source and frequency signals
engine.set_module_weights(
    5.0,   // preprocessing
    25.0,  // source_validation
    10.0,  // phrase_indexing
    10.0,  // kmp
    10.0,  // rabin_karp
    20.0,  // frequency
    10.0,  // temporal
    10.0   // greedy
);
```

---

## Benchmarks

Measured on a 32-thread machine with `-O3`:

| Metric | Result |
|---|---|
| Throughput | ~71,428 articles/sec |
| Latency P50 | < 1 ms |
| Latency P99 | < 2 ms |
| Memory (1,000 articles) | ~0.85 MB |

Benchmarks are located in `benchmark/` and cover throughput (`benchmark_throughput`), tail latency (`benchmark_latency`), and heap usage (`benchmark_memory`).

---

## Project Structure

```
NewsScope/
├── include/                  # Public headers
│   ├── types.h               # Article, CredibilityResult, shared structs
│   ├── scoring_engine.h      # Main engine interface
│   ├── preprocessing.h
│   ├── source_validator.h
│   ├── phrase_indexer.h
│   ├── string_matcher.h
│   ├── frequency_analyzer.h
│   ├── temporal_analyzer.h
│   ├── greedy_filter.h
│   └── thread_pool.h
├── src/                      # Module implementations + demo entrypoint
├── tests/                    # Unit and integration tests (one file per module)
├── benchmark/                # Throughput, latency, and memory benchmarks
├── data/                     # Sample sources.csv, suspicious_phrases.txt, articles
├── docs/                     # Architecture, complexity analysis, class diagrams
├── Makefile
└── CMakeLists.txt
```

---

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — component design and concurrency model
- [`docs/COMPLEXITY_ANALYSIS.md`](docs/COMPLEXITY_ANALYSIS.md) — per-module time and space analysis
- [`docs/CLASS_DIAGRAMS.txt`](docs/CLASS_DIAGRAMS.txt) — class relationships
- [`docs/EXAMPLES.md`](docs/EXAMPLES.md) — extended usage examples
- [`docs/PROJECT_REPORT.md`](docs/PROJECT_REPORT.md) — full project report

---

## Design Goals

- No machine learning or external model dependencies
- Deterministic, reproducible output for the same input
- Fully explainable scores — every module reports its reasoning
- Performance-oriented C++17 with `O3` optimization and thread-safe batch APIs
