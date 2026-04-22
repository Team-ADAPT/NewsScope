# Data Assets

This directory contains all local data files consumed by the NewsScope scoring engine and ML overlay.

---

## articles.json

Labeled article dataset used for ML model training and integration testing.

**Schema:**
```json
{
  "id":       "string  — unique article identifier",
  "headline": "string  — article title",
  "body":     "string  — article body text",
  "source":   "string  — publisher or subject category",
  "label":    "credible | non_credible | neutral"
}
```

**Label mapping used by `ml/model_inference.py`:**

| label | ML class |
|---|---|
| `credible` | 1 |
| `non_credible` | 0 |
| `neutral` | skipped (not used for training) |

**Current stats:** ~977 articles — 470 credible, 473 non-credible, 34 neutral.

**ID prefix conventions (legacy):**

| prefix | label |
|---|---|
| `trusted-`, `trust-` | credible |
| `fake-`, `untrusted-` | non_credible |
| `neutral-` | neutral |
| `ext-credible-` | credible (external dataset) |
| `ext-fake-` | non_credible (external dataset) |

External articles (`ext-*`) were sourced from the [Fake and Real News Dataset](https://www.kaggle.com/datasets/clmentbisaillon/fake-and-real-news-dataset) (Kaggle).

---

## sources.csv

Source credibility map used by `SourceValidator`.

**Schema:**
```
source,status,credibility
```

- `source` — publisher name (lowercase)
- `status` — `trusted` or `untrusted`
- `credibility` — integer score 0–100

---

## negative_terms.csv

Weighted suspicious terms and phrases used by `FrequencyAnalyzer`.

**Schema:**
```
term,weight
```

- `term` — word or phrase to match
- `weight` — float 0.0–1.0 (higher = more suspicious)

---

## suspicious_phrases.txt

One phrase per line. Loaded into a trie by `PhraseIndexer` for fast multi-pattern detection.

---

## ml/

ML model artifacts. These are workspace-only files generated on demand.

| File | Description |
|---|---|
| `tfidf_logreg.joblib` | Serialized TF-IDF + Logistic Regression pipeline |
| `tokenizer.json` | Training metadata (example counts, version, feature config) |

Regenerate by deleting both files and running any inference call — `model_inference.py` will retrain automatically from `articles.json`.

To retrain manually:
```bash
rm data/ml/tfidf_logreg.joblib data/ml/tokenizer.json
python3 ml/model_inference.py \
  --model data/ml/tfidf_logreg.joblib \
  --tokenizer data/ml/tokenizer.json \
  --text-file <any_text_file> \
  --articles data/articles.json
```
