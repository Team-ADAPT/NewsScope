# NewsScope Project Report (Phase 2 & 3)

## 1. Executive Summary

**NewsScope** is a deterministic, high-performance news credibility assessment platform built in **C++17** without ML in the current core pipeline.

It accepts article text and source metadata, then returns:

- credibility score (`0-100`)
- module-wise score contribution
- human-readable explanation per module

The implementation is modular, test-driven, and benchmarked for scalability-focused academic evaluation.

## 2. Team-Wise Algorithm Ownership

> The algorithmic work is divided across four teammates as requested.

| Teammate | Assigned Algorithmic Scope | Current Status |
| --- | --- | --- |
| **Anurag** | Scoring Engine integration, Claim Verifiability calibration, ThreadPool + concurrency wiring | Completed |
| **Divynashi** | Preprocessing pipeline + Frequency Analysis + data normalization strategy | Completed |
| **Tanishk** | Source Validation (hash-based), Phrase Indexing (Trie), temporal signal design | Completed |
| **Prajjwal** | String Matching stack (KMP + Rabin-Karp), Greedy Filtering heuristics, pattern signal tuning | Completed |

## 3. Detailed Implementation by Teammate

### 3.1 Anurag

- Integrated all module outputs into a weighted `ScoringEngine`.
- Added claim-verifiability based calibration to improve real-vs-fake separation.
- Implemented and tuned multithreaded processing support through `ThreadPool`.
- Added web background-job analysis integration and verdict gating logic.

### 3.2 Divynashi

- Implemented tokenization, stop-word removal, normalization flow in preprocessing.
- Built weighted negative-term `FrequencyAnalyzer` using `unordered_map`.
- Supported extensible negative-term data loading from `data/negative_terms.csv`.
- Contributed to data quality checks and robustness improvements.

### 3.3 Tanishk

- Implemented `SourceValidator` with hash-based average `O(1)` lookup.
- Improved source normalization (trim/lowercase/whitespace handling).
- Implemented Trie-based suspicious phrase indexing and lookup.
- Contributed temporal sliding-window analysis design and integration.

### 3.4 Prajjwal

- Implemented deterministic KMP matcher with LPS computation.
- Implemented Rabin-Karp rolling hash multi-pattern matching with collision-safe checks.
- Implemented greedy manipulation/clickbait signal prioritization.
- Tuned suspicious phrase and pattern matching behavior for risk detection.

## 4. Core Modules and Complexity

| Module | Time Complexity | Space Complexity | Notes |
| --- | --- | --- | --- |
| Preprocessing | `O(n)` | `O(n)` | tokenization + normalization |
| Source Validation | `O(1)` avg | `O(s)` | hash-table based |
| Phrase Indexing (Trie) | `O(m)` insert/search | `O(P*m)` | prefix-efficient |
| KMP | `O(n+m)` | `O(m)` | deterministic exact matching |
| Rabin-Karp | `O(n+m)` avg (`O(n*m)` worst) | `O(1)` per pattern | rolling hash + verification |
| Frequency Analysis | `O(t)` | `O(u)` | weighted suspicious term counts |
| Temporal Analysis | amortized `O(1)` updates | `O(w)` | deque sliding window |
| Greedy Filtering | `O(n)` detect + `O(k log k)` rank | `O(k)` | strongest-signal prioritization |
| Scoring Aggregation | `O(1)` | `O(1)` | weighted composition |

Legend: `n` text length, `m` pattern length, `t` token count, `u` unique tokens, `w` window size, `k` detected signals.

## 5. Current Validation Status

- Clean build and full validation completed:
  - `make clean && make all`
  - `make run-tests`
  - `make run-benchmarks`
- Web/API smoke tests passed (`/`, `/api/jobs`, job polling).
- Burst concurrency checks passed (parallel background jobs completed successfully).
- Real-vs-fake sample separation validated after calibration fixes.

## 6. Build and Usage

```bash
make all
./build/newsscope_demo
make run-web
make run-tests
make run-benchmarks
```

Core code usage:

```cpp
ScoringEngine engine;
engine.initialize("data/sources.csv", "data/suspicious_phrases.txt", "data/negative_terms.csv");
CredibilityResult result = engine.assess_article(article);
```

## 7. Team-Wise Future Work Allocation (Including ML Scope)

> Current system remains non-ML by design. ML items below are strictly future enhancements.

### 7.1 Anurag — Platform Hardening

- External configuration for weights/thresholds (remove hardcoded calibration constants).
- Persistent result storage for web jobs.
- Add health checks, metrics, and production diagnostics endpoints.

### 7.2 Divynashi — Data & NLP Feature Expansion

- Add n-gram support (bi-gram/tri-gram) for multi-word suspicious terms.
- Improve dataset curation/quality checks and automated validation scripts.
- Add richer linguistic cue libraries for deterministic (non-ML) scoring.

### 7.3 Tanishk — Performance & Deployment

- Dockerize service and add deployment templates.
- Add CI pipeline for build, test, and benchmark smoke checks.
- Improve load-handling policy (adaptive backpressure and queue control).

### 7.4 Prajjwal — ML Research Track (Future Phase)

- Design optional **hybrid ML layer** (separate from deterministic core) for comparative analysis.
- Evaluate classical vs ML-assisted credibility classification using labeled datasets.
- Explore explainable models (e.g., interpretable feature-based classifiers) while preserving module explainability.
- Keep ML behind feature flags so deterministic mode remains default for academic compliance.

## 8. Deliverables Status

- Algorithmic core modules: **Completed**
- Explainable scoring engine: **Completed**
- Threaded batch processing: **Completed**
- Web portal + background processing: **Completed**
- Tests + integration checks: **Completed**
- Benchmarks (throughput/latency/memory): **Completed**
- Team-wise roadmap and ML future scope: **Completed (documented)**

## 9. Conclusion

NewsScope is in a strong presentation-ready state with complete deterministic algorithmic implementation and clear teammate-wise ownership. The next phase is focused on hardening, deployment maturity, and optional ML expansion as a future, controlled extension.
