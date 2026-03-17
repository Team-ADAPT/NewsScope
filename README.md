# NewsScope

Deterministic, high-performance news credibility assessment engine in C++17.

NewsScope evaluates each article using classical algorithms only (no machine learning), then returns:

- `overall_score` in the range `0-100`
- per-module score breakdown
- human-readable explanation of how each module contributed

## Why NewsScope

NewsScope is designed for academic and systems engineering use cases where explainability, determinism, and performance matter more than model training.

Key properties:

- Algorithmic and deterministic behavior
- Modular architecture with clean interfaces
- Concurrent processing via a thread pool
- Benchmark and test coverage included

## Core modules

| Module | Primary technique | Time complexity (typical) | Notes |
| --- | --- | --- | --- |
| Preprocessing | Tokenization + normalization + stop-word removal | `O(n)` | Efficient text cleanup |
| Source Validation | `unordered_map` lookup | `O(1)` avg | Trusted/untrusted source scoring |
| Phrase Indexing | Trie | `O(m)` insert/search | Fast suspicious phrase detection |
| String Matching (KMP) | Prefix/LPS-based match | `O(n+m)` | Deterministic linear matching |
| String Matching (Rabin-Karp) | Rolling hash | `O(n+m)` avg | Multi-pattern friendly |
| Frequency Analysis | Hash-map counting | `O(n)` | Suspicious term frequency scoring |
| Temporal Analysis | Sliding window (`deque`) | Amortized `O(1)` updates | Spike detection over time |
| Greedy Filtering | Priority-driven signals | `O(k log k)` | Clickbait/manipulation prioritization |
| Scoring Engine | Weighted aggregation | `O(1)` | Combines module outputs |

`n`: text length, `m`: pattern length, `k`: detected signal count.

## System architecture

```text
Article(headline, body, source, timestamp)
        |
        v
  Preprocessing
        |
        +--> Source Validator (hash lookup)
        +--> Phrase Indexer (trie scan)
        +--> KMP Matcher
        +--> Rabin-Karp Matcher
        +--> Frequency Analyzer
        +--> Temporal Analyzer
        +--> Greedy Filter
                 |
                 v
          Scoring Engine
                 |
                 v
CredibilityResult(score, module_scores, explanations, processing_time)
```

## Scoring model

By default, NewsScope uses equal weights across the eight scoring modules (`12.5%` each).  
Weights are configurable through `ScoringEngine::set_module_weights(...)`.

## Concurrency and scalability

- `ThreadPool` supports configurable worker counts
- Batch assessment API supports high-throughput workloads
- Locking is minimized to protect shared mutable state
- Suitable architecture for handling large article streams and high request concurrency

## Project layout

```text
NewsScope/
├── include/         # Public headers
├── src/             # Implementations + demo entrypoint
├── tests/           # Unit and integration tests
├── benchmark/       # Throughput, latency, memory benchmarks
├── data/            # Sample sources, phrases, articles
├── docs/            # Architecture, complexity, examples, class diagrams
├── Makefile
└── CMakeLists.txt
```

## Build and run

Prerequisites:

- C++17 compiler (`clang++` or `g++`)
- `make`

Commands:

```bash
make all
./build/newsscope_demo
make run-tests
make run-benchmarks
```

Clean artifacts:

```bash
make clean
```

## Quick usage

```cpp
#include "scoring_engine.h"
using namespace newsscope;

int main() {
    ScoringEngine engine;
    engine.initialize("data/sources.csv", "data/suspicious_phrases.txt", "");

    Article article(
        "id-1",
        "Breaking update on policy decision",
        "Detailed body text goes here...",
        "Reuters"
    );

    CredibilityResult result = engine.assess_article(article);
    return (result.overall_score >= 0.0) ? 0 : 1;
}
```

## Verification status

Current repository status (latest local run):

- Build: successful
- Tests: all passing (`make run-tests`)
- Benchmarks: successful (`throughput`, `latency`, `memory`)

Observed benchmark snapshot from this environment:

| Metric | Observed |
| --- | --- |
| Throughput (32 threads) | ~71,428 articles/sec |
| Latency P50 | < 1 ms (small/medium inputs) |
| Latency P99 | < 2 ms |
| Memory (1000 articles) | ~0.85 MB |

## Documentation

Detailed references are available in:

- `docs/ARCHITECTURE.md`
- `docs/COMPLEXITY_ANALYSIS.md`
- `docs/CLASS_DIAGRAMS.txt`
- `docs/EXAMPLES.md`

## Constraints and design goals

- No machine learning
- No heavy external libraries
- Deterministic, explainable scoring
- Performance-oriented C++ implementation suitable for Design and Analysis of Algorithms coursework
