# NewsScope

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)
![Interface](https://img.shields.io/badge/interface-web%20%2B%20cli-c4672f.svg)

NewsScope is a deterministic credibility analysis engine for news text, built in C++17.

It evaluates article input using transparent scoring modules for source trust, claim grounding, suspicious phrasing, manipulation patterns, and supporting context, then returns a score with explanations instead of a black-box verdict.

## At a Glance

- deterministic, explainable scoring
- native C++ engine with local web interface
- source, language, and claim-based analysis in one pipeline
- benchmarked and test-covered
- deterministic core with optional TF-IDF + Logistic Regression ML overlay

## Why It Exists

Most fake-news detectors either behave like opaque classifiers or collapse everything into one confidence number. NewsScope takes the opposite approach:

- every major signal is visible
- every module can be tested independently
- the result can be explained in plain language
- the same input produces the same output every time

This makes it more suitable for demos, academic work, local tooling, and systems where reproducibility matters.

## Core Pipeline

NewsScope combines multiple modules, each responsible for one kind of signal:

| Module | Purpose |
|---|---|
| `Preprocessing` | normalize and tokenize article text |
| `Source Validation` | score known publishers from a source credibility map |
| `Phrase Indexing` | detect suspicious phrases through a trie |
| `KMP Matching` | perform deterministic string-pattern checks |
| `Rabin-Karp` | run efficient multi-pattern matching |
| `Frequency Analysis` | score weighted suspicious terms and phrases |
| `Temporal Analysis` | detect unusual activity spikes |
| `Greedy Filter` | flag clickbait and manipulation-heavy language |
| `Claim Verifier` | estimate how grounded and attributable the claims are |
| `Scoring Engine` | calibrate the final credibility score |

## Output

Each run returns:

- overall credibility score
- module-level scores
- explanation lines
- processing time

Typical interpretation:

| Score | Signal |
|---|---|
| `80-100` | strong credibility signal |
| `60-79` | generally credible with some uncertainty |
| `40-59` | mixed or weakly grounded |
| `0-39` | high-risk or likely misleading |

## Demo Experience

### Web Interface

Start the local server:

```bash
make run-web
```

Open:

```text
http://localhost:8080
```

The web app supports:

- article text submission
- optional source input
- background analysis jobs
- verdict plus score display
- module-by-module breakdown
- explanation cards for the detected signals

### Native API

```cpp
#include "scoring_engine.h"

using namespace newsscope;

ScoringEngine engine;
engine.initialize();

Article article(
    "article-1",
    "Central bank holds benchmark interest rate steady",
    "In its official statement, the central bank said inflation has eased...",
    "Reuters"
);

CredibilityResult result = engine.assess_article(article);
```

## Quick Start

### Requirements

- `clang++` or `g++` with C++17 support
- `make`

### Build

```bash
git clone https://github.com/Team-ADAPT/NewsScope.git
cd NewsScope
make all
```

### Common Commands

```bash
make run            # CLI demo
make run-web        # web server
make run-tests      # test suite
make run-benchmarks # throughput, latency, memory
make clean          # remove build artifacts
```

### CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Repository Structure

```text
NewsScope/
├── include/      public headers
├── src/          scoring engine, modules, demo, web server
├── tests/        unit and integration coverage
├── benchmark/    performance benchmarks
├── ml/           optional local model inference helper
├── data/         source maps, weighted terms, phrase lists, article samples
├── web/          browser UI assets
└── docs/         architecture and supporting notes
```

## Data Assets

The repository includes local data files used directly by the engine:

- `data/articles.json`
  sample and evaluation articles
- `data/sources.csv`
  source credibility data
- `data/negative_terms.csv`
  weighted suspicious terms and phrases
- `data/suspicious_phrases.txt`
  trie-loaded phrase patterns

`ScoringEngine::initialize()` auto-loads these files when they are present in the standard project layout.

## Testing

Run the full suite:

```bash
make run-tests
```

Coverage includes:

- claim verification
- source validation
- string matching
- phrase indexing
- preprocessing
- frequency analysis
- temporal analysis
- greedy filtering
- end-to-end scoring integration

## Benchmarks

Run the benchmark targets individually:

```bash
make run-throughput
make run-latency
make run-memory
```

These are useful when tuning heuristics, changing data files, or validating UI and scoring updates against runtime cost.

## Architecture Snapshot

```text
Article
  -> Preprocessor
  -> SourceValidator
  -> PhraseIndexer
  -> StringMatcher
  -> FrequencyAnalyzer
  -> TemporalAnalyzer
  -> GreedyFilter
  -> ClaimVerifier
  -> ScoringEngine
  -> CredibilityResult
```

The engine is intentionally modular: each detector remains individually testable while the scoring engine stays responsible for aggregation and calibration.

## Design Principles

- `Explainability first`
  results should be understandable without reverse-engineering the code
- `Deterministic behavior`
  identical input should not drift between runs
- `Practical performance`
  the system should feel instant for local use
- `Composable modules`
  heuristics should be tunable without rewriting the whole pipeline

## Documentation

Additional material is available in [`docs/`](./docs):

- [`ARCHITECTURE.md`](./docs/ARCHITECTURE.md)
- [`COMPLEXITY_ANALYSIS.md`](./docs/COMPLEXITY_ANALYSIS.md)
- [`CLASS_DIAGRAMS.txt`](./docs/CLASS_DIAGRAMS.txt)
- [`EXAMPLES.md`](./docs/EXAMPLES.md)
- [`PROJECT_REPORT.md`](./docs/PROJECT_REPORT.md)

## Current State

NewsScope currently includes:

- a native scoring engine
- a local web application
- test and benchmark tooling
- repository branch protection on active branches
- optional local TF-IDF + Logistic Regression inference (disabled by default; opt in with an environment variable)

## Optional ML Overlay

The deterministic scoring pipeline remains the primary credibility engine.  
ML is opt-in because the deterministic path is the default low-latency runtime.

When ML is enabled, NewsScope uses confidence-gated blending:

`Final Score = 0.75 * deterministic_score + 0.25 * ml_score`

If ML confidence is low, blending is skipped to avoid degrading clearly grounded real-news assessments.

The ML side uses Python + scikit-learn (`TF-IDF` + `LogisticRegression`) via `ml/model_inference.py`.

Environment controls:

- `NEWSSCOPE_ENABLE_ML=1` enables model inference
- `NEWSSCOPE_ML_BLEND_WEIGHT=0.0..1.0` blends deterministic and ML-derived scores (default `0.0`)
- `NEWSSCOPE_ML_TOKENIZER_PATH=<path>` overrides tokenizer artifact path (default `data/ml/tokenizer.json`)
- `NEWSSCOPE_ML_MODEL_PATH=<path>` overrides the serialized model path (default `data/ml/tfidf_logreg.joblib`)

Local ML artifacts are intentionally treated as workspace-only files:

- `data/ml/tfidf_logreg.joblib` is generated on demand by `ml/model_inference.py`
- `data/ml/tokenizer.json` stores ML metadata generated by `ml/model_inference.py`

## License

There is no explicit `LICENSE` file in the repository root right now. If you plan to distribute this project publicly, add one first.
