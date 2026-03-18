# Complexity Analysis

## 1) Preprocessing
- Time: Best/Average/Worst = O(n)
- Space: O(n)
- Trade-off: Linear scan is fast; full token materialization costs memory.

## 2) Hash-Based Source Validation
- Time: Best O(1), Average O(1), Worst O(s)
- Space: O(s)
- Trade-off: Very fast expected lookup; collision-heavy worst-case degrades.

## 3) Trie Phrase Indexing
- Insert/Search: O(m)
- Text scan for known phrases: O(n * L) worst-path bounded by phrase depth
- Space: O(P * m)
- Trade-off: Prefix-friendly and deterministic; higher node overhead than flat hash.

## 4a) KMP
- Time: Best/Average/Worst O(n + m)
- Space: O(m)
- Trade-off: Deterministic linear behavior, excellent for single pattern.

## 4b) Rabin-Karp
- Time: Best/Average O(n + m), Worst O(n*m) (collision-heavy)
- Space: O(1) per pattern
- Trade-off: Great for multi-pattern workloads; requires collision verification.

## 5) Frequency Analysis
- Time: O(t)
- Space: O(u)
- Trade-off: Simple and robust; quality depends on curated suspicious-term weights.

## 6) Sliding Window Temporal Analysis
- Add/Cleanup: amortized O(1)
- Spike computation: O(w)
- Space: O(w)
- Trade-off: Fast online updates; exact spike stats depend on window size.

## 7) Greedy Filtering
- Pattern detection: O(n)
- Prioritization: O(k log k)
- Space: O(k)
- Trade-off: Explainable strongest-signal prioritization; heuristic severity tuning needed.

## 8) Scoring Engine
- Combination: O(1)
- End-to-end: dominated by text processing modules (~O(n))
- Trade-off: Interpretable weighted model; calibration affects final score distribution.
