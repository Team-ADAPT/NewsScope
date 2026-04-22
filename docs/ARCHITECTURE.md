# NewsScope Architecture

## High-level Flow
1. Ingest `Article{headline, body, source, timestamp}`
2. Preprocess text (tokenize, normalize, stop-word removal)
3. Run parallelizable credibility signals:
   - Source hash validation
   - Trie phrase detection
   - KMP pattern match
   - Rabin-Karp multi-pattern match
   - Frequency anomaly detection
   - Temporal spike analysis
   - Greedy clickbait/manipulation filtering
4. Compute deterministic credibility score from algorithmic modules
5. Optionally blend with ML enhancement (`TF-IDF + Logistic Regression`) as:
   `Final = 0.75 * deterministic + 0.25 * ml`
   (blend is confidence-gated to avoid low-confidence ML drift)
6. Return `CredibilityResult{score, module_scores, explanations, latency}`

## Components
- `Preprocessor`: O(n) text pipeline
- `SourceValidator`: hash-table credibility lookup
- `PhraseIndexer`: Trie for suspicious phrase detection
- `StringMatcher`: KMP and Rabin-Karp
- `FrequencyAnalyzer`: weighted suspicious-term frequency
- `TemporalAnalyzer`: deque-based sliding window spike detection
- `GreedyFilter`: priority-based strongest-signal selection
- `ScoringEngine`: orchestrates modules and explainability
- `ThreadPool`: concurrent task execution

## Concurrency Design
- `ThreadPool` uses task queue + worker pool + condition variables.
- `ScoringEngine` uses mutex-protected assessment path for safe shared-state access.
- Benchmark path demonstrates thread-local engines for high-throughput workloads.

## Scalability Notes
- Hash lookups and token counting are cache-friendly.
- Trie avoids repeated full-string scans for known phrase sets.
- Sliding window uses amortized O(1) push/pop cleanup.
- Batch APIs enable lower scheduling overhead.
