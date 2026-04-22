#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import List, Optional, Tuple

import joblib
import numpy as np
from sklearn.feature_extraction.text import TfidfVectorizer
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import Pipeline


DEFAULT_MAX_FEATURES = 5000
MODEL_VERSION = 3
MODEL_TYPE = "tfidf_logreg_v3"

POSITIVE_SEED_TEXTS = [
    "According to an official report, the ministry said audited data was published.",
    "The court filing confirms the committee vote and includes primary source evidence.",
    "Reuters reported that officials issued a statement with verifiable statistics.",
]

NEGATIVE_SEED_TEXTS = [
    "Shocking truth exposed anonymous insiders confirmed with no official confirmation.",
    "You won't believe this one trick they are hiding the secret agenda from everyone.",
    "Share before deletion anonymous source claims a cover-up without evidence.",
]


def sanitize_message(message: str) -> str:
    return message.replace("\n", " ").replace("|", "/").strip()


def infer_binary_label(article: dict) -> Optional[int]:
    for key in ("label", "target", "class"):
        if key in article:
            value = str(article[key]).strip().lower()
            if value in {"1", "real", "true", "trusted", "credible"}:
                return 1
            if value in {"0", "fake", "false", "untrusted", "misleading", "non_credible"}:
                return 0

    if "is_fake" in article:
        try:
            return 0 if bool(article["is_fake"]) else 1
        except Exception:
            pass

    article_id = str(article.get("id", "")).strip().lower()
    if article_id.startswith(("trusted-", "trust-", "ext-credible-")):
        return 1
    if article_id.startswith(("untrusted-", "fake-", "ext-fake-")):
        return 0

    source = str(article.get("source", "")).strip().lower()
    if "fake news" in source or "hoax" in source:
        return 0
    return None


def load_training_rows(articles_path: Path) -> Tuple[List[str], List[int]]:
    if not articles_path.exists():
        return [], []

    with articles_path.open("r", encoding="utf-8") as handle:
        parsed = json.load(handle)
    if not isinstance(parsed, list):
        return [], []

    texts: List[str] = []
    labels: List[int] = []
    for article in parsed:
        if not isinstance(article, dict):
            continue
        label = infer_binary_label(article)
        if label is None:
            continue
        headline = str(article.get("headline", ""))
        body = str(article.get("body", ""))
        text = f"{headline} {body}".strip()
        if not text:
            continue
        texts.append(text)
        labels.append(label)

    return texts, labels


def train_pipeline(articles_path: Path, max_features: int) -> Tuple[Pipeline, dict]:
    texts, labels = load_training_rows(articles_path)

    # Stabilize decision boundaries for out-of-distribution real/fake phrasing.
    texts.extend(POSITIVE_SEED_TEXTS)
    labels.extend([1] * len(POSITIVE_SEED_TEXTS))
    texts.extend(NEGATIVE_SEED_TEXTS)
    labels.extend([0] * len(NEGATIVE_SEED_TEXTS))

    if len(texts) < 20:
        raise RuntimeError("Not enough labeled samples to train TF-IDF + Logistic Regression model")

    unique_labels = set(labels)
    if len(unique_labels) < 2:
        raise RuntimeError("Training data does not contain both classes")

    pipeline = Pipeline(
        steps=[
            (
                "tfidf",
                TfidfVectorizer(
                    lowercase=True,
                    max_features=max_features,
                    ngram_range=(1, 2),
                    sublinear_tf=True,
                    strip_accents="unicode",
                ),
            ),
            (
                "logreg",
                LogisticRegression(
                    solver="liblinear",
                    max_iter=500,
                    random_state=42,
                    class_weight="balanced",
                ),
            ),
        ]
    )
    pipeline.fit(texts, labels)

    metadata = {
        "model_type": "tfidf_logreg",
        "trained_examples": len(texts),
        "credible_examples": int(np.sum(np.array(labels) == 1)),
        "non_credible_examples": int(np.sum(np.array(labels) == 0)),
        "source": articles_path.as_posix(),
        "max_features": max_features,
        "class_weight": "balanced",
    }
    return pipeline, metadata


def write_metadata(tokenizer_path: Path, metadata: dict) -> None:
    tokenizer_path.parent.mkdir(parents=True, exist_ok=True)
    with tokenizer_path.open("w", encoding="utf-8") as handle:
        json.dump(metadata, handle, ensure_ascii=True)


def load_or_train_pipeline(model_path: Path, tokenizer_path: Path, articles_path: Path, max_features: int) -> Tuple[Pipeline, str]:
    if model_path.exists():
        payload = joblib.load(model_path)
        if isinstance(payload, dict) and "pipeline" in payload:
            version = int(payload.get("version", 0))
            model_type = str(payload.get("model_type", ""))
            if version >= MODEL_VERSION and model_type == MODEL_TYPE:
                pipeline = payload["pipeline"]
                if not hasattr(pipeline, "predict_proba"):
                    raise RuntimeError("Serialized model does not support predict_proba")
                return pipeline, "loaded model"
        elif hasattr(payload, "predict_proba"):
            return payload, "loaded model (legacy format)"

    pipeline, metadata = train_pipeline(articles_path=articles_path, max_features=max_features)
    model_path.parent.mkdir(parents=True, exist_ok=True)
    metadata["model_type"] = MODEL_TYPE
    metadata["version"] = MODEL_VERSION
    joblib.dump({"version": MODEL_VERSION, "model_type": MODEL_TYPE, "pipeline": pipeline}, model_path)
    write_metadata(tokenizer_path, metadata)
    return pipeline, "trained from local articles dataset"


def predict_credible_probability(pipeline: Pipeline, text: str) -> float:
    probabilities = pipeline.predict_proba([text])[0]
    classes = getattr(pipeline, "classes_", None)
    if classes is None:
        classes = getattr(pipeline.named_steps.get("logreg"), "classes_", [0, 1])  # type: ignore[arg-type]

    classes_list = list(classes)
    if 1 in classes_list:
        idx = classes_list.index(1)
    elif len(probabilities) > 1:
        idx = 1
    else:
        idx = 0
    return float(probabilities[idx])


def main() -> int:
    parser = argparse.ArgumentParser(description="NewsScope ML inference (TF-IDF + Logistic Regression)")
    parser.add_argument("--model", required=True, help="Path to serialized model (.joblib)")
    parser.add_argument("--tokenizer", required=True, help="Path to metadata json (compatibility field)")
    parser.add_argument("--text-file", required=True, help="Path to input text file")
    parser.add_argument("--articles", default="data/articles.json", help="Dataset used when model training is needed")
    parser.add_argument("--vocab-size", type=int, default=DEFAULT_MAX_FEATURES, help="Max TF-IDF features")
    parser.add_argument("--max-len", type=int, default=0, help="Unused legacy argument (kept for compatibility)")
    args = parser.parse_args()

    try:
        model_path = Path(args.model)
        tokenizer_path = Path(args.tokenizer)
        articles_path = Path(args.articles)
        text_path = Path(args.text_file)

        if not text_path.exists():
            print("ERR|Input text file not found")
            return 1

        text = text_path.read_text(encoding="utf-8")
        pipeline, details = load_or_train_pipeline(
            model_path=model_path,
            tokenizer_path=tokenizer_path,
            articles_path=articles_path,
            max_features=int(args.vocab_size),
        )

        probability = predict_credible_probability(pipeline=pipeline, text=text)
        probability = max(0.0, min(1.0, probability))
        print(f"OK|{probability:.8f}|{sanitize_message('TF-IDF + Logistic Regression, ' + details)}")
        return 0
    except Exception as exc:
        print(f"ERR|{sanitize_message(str(exc))}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
