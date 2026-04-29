const form = document.getElementById("analyze-form");
const submitBtn = document.getElementById("submit-btn");
const statusBox = document.getElementById("status-box");
const resultBox = document.getElementById("result-box");
const emptyState = document.getElementById("empty-state");

const resultLabel = document.getElementById("result-label");
const resultBadge = document.getElementById("result-badge");
const resultMeta = document.getElementById("result-meta");
const resultInsights = document.getElementById("result-insights");
const moduleScores = document.getElementById("module-scores");
const explanations = document.getElementById("explanations");
const scoreNumber = document.getElementById("score-number");
const scoreRingFill = document.getElementById("score-ring-fill");
const scoreCircle = document.getElementById("score-circle");

const tabTextBtn = document.getElementById("tab-text");
const tabLinkBtn = document.getElementById("tab-link");
const panelText = document.getElementById("panel-text");
const panelLink = document.getElementById("panel-link");
const textInput = document.getElementById("text-input");
const urlInput = document.getElementById("url-input");
const sourceInput = document.getElementById("source-input");

const CIRCUMFERENCE = 2 * Math.PI * 54;
let scoreAnimationFrame = null;
let activeInputMode = "text";

function normalizeScore(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return 0;
  }
  return Math.max(0, Math.min(100, numeric));
}

function resetScoreRing() {
  if (scoreAnimationFrame !== null) {
    cancelAnimationFrame(scoreAnimationFrame);
    scoreAnimationFrame = null;
  }

  scoreCircle.style.setProperty("--progress", "0");
  scoreNumber.textContent = "0";
}

function showStatus(message, tone = "working") {
  statusBox.textContent = message;
  statusBox.classList.remove("hidden", "status-error", "status-done");

  if (tone === "error") {
    statusBox.classList.add("status-error");
  } else if (tone === "done") {
    statusBox.classList.add("status-done");
  }
}

function clearResult() {
  resultBox.classList.add("hidden");
  emptyState.classList.remove("hidden");
  moduleScores.innerHTML = "";
  explanations.innerHTML = "";
  resultInsights.innerHTML = "";
  resultMeta.textContent = "";
  resultLabel.textContent = "";
  resetScoreRing();
  scoreRingFill.classList.remove("high", "medium", "low");
  scoreCircle.classList.remove("tone-high", "tone-medium", "tone-low");
}

function getScoreClass(score) {
  if (score >= 75) return "high";
  if (score >= 50) return "medium";
  return "low";
}

function formatModuleName(name) {
  return name
    .replace(/_/g, " ")
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

function animateScore(targetScore) {
  const safeTargetScore = normalizeScore(targetScore);
  const duration = 1100;
  const startTime = performance.now();
  const startValue = 0;

  resetScoreRing();
  // Force the browser to apply the reset before starting a new animation.
  scoreCircle.getBoundingClientRect();

  function update(currentTime) {
    const elapsed = currentTime - startTime;
    const progress = Math.min(elapsed / duration, 1);
    const easeOut = 1 - Math.pow(1 - progress, 3);
    const currentScore = startValue + (safeTargetScore - startValue) * easeOut;

    scoreNumber.textContent = Math.round(currentScore);
    scoreCircle.style.setProperty("--progress", currentScore.toFixed(2));

    if (progress < 1) {
      scoreAnimationFrame = requestAnimationFrame(update);
    } else {
      scoreAnimationFrame = null;
      scoreNumber.textContent = Math.round(safeTargetScore);
      scoreCircle.style.setProperty("--progress", safeTargetScore.toFixed(2));
    }
  }

  scoreAnimationFrame = requestAnimationFrame(update);
}

function createInsightCard(title, value, tone) {
  const item = document.createElement("div");
  item.className = `insight-card tone-${tone}`;
  item.innerHTML = `
    <span class="insight-title">${title}</span>
    <strong class="insight-value">${value}</strong>
  `;
  return item;
}

function buildInsights(modules, score) {
  const source = Number(modules.source_validation || 0);
  const claim = Number(modules.claim_verifiability || 0);
  const greedy = Number(modules.greedy_filtering || 0);

  const confidenceBand =
    score >= 75 ? "Stable" : score >= 50 ? "Mixed" : "Fragile";
  const languageRisk =
    greedy >= 70 ? "Low manipulation" : greedy >= 40 ? "Some pressure" : "High manipulation";
  const sourceSignal =
    source >= 80 ? "Trusted outlet" : source >= 50 ? "Unknown or mixed source" : "Weak source";
  const claimSignal =
    claim >= 70 ? "Well grounded" : claim >= 45 ? "Partially grounded" : "Weakly supported";

  resultInsights.appendChild(createInsightCard("Confidence Band", confidenceBand, getScoreClass(score)));
  resultInsights.appendChild(createInsightCard("Source Signal", sourceSignal, getScoreClass(source)));
  resultInsights.appendChild(createInsightCard("Claim Support", claimSignal, getScoreClass(claim)));
  resultInsights.appendChild(createInsightCard("Language Risk", languageRisk, getScoreClass(greedy)));
}

function renderExplanations(lines) {
  lines.forEach((line, index) => {
    const li = document.createElement("li");
    li.className = "explain-item";
    li.style.animationDelay = `${index * 50}ms`;

    const match = line.match(/^\[([^\]]+)\]\s*(.*)$/);
    if (match) {
      const [, title, body] = match;
      li.innerHTML = `
        <span class="explain-tag">${title}</span>
        <span class="explain-text">${body}</span>
      `;
    } else {
      li.textContent = line;
    }
    explanations.appendChild(li);
  });
}

function renderModules(modules) {
  console.log("[NewsScope Debug] Module scores received from API:", JSON.stringify(modules));
  Object.entries(modules)
    .sort((a, b) => Number(b[1]) - Number(a[1]))
    .forEach(([name, value], index) => {
      const numValue = normalizeScore(value);
      const moduleClass = getScoreClass(numValue);

      console.log(`[NewsScope Debug] ${name}: raw=${value}, normalized=${numValue}, class=${moduleClass}`);

      // SAFETY: if score is exactly 100 and this is a detection module, flag it
      if (numValue === 100) {
        console.warn(`[NewsScope Warning] Module "${name}" scored exactly 100 — verify backend computation`);
      }

      const item = document.createElement("div");
      item.className = "module-item";
      item.style.animationDelay = `${index * 45}ms`;

      item.innerHTML = `
        <div class="module-topline">
          <span class="module-name">${formatModuleName(name)}</span>
          <span class="module-score ${moduleClass}">${numValue.toFixed(0)}%</span>
        </div>
        <div class="module-bar">
          <div class="module-bar-fill ${moduleClass}" style="width: 0%"></div>
        </div>
      `;

      moduleScores.appendChild(item);

      requestAnimationFrame(() => {
        const fill = item.querySelector(".module-bar-fill");
        fill.style.width = `${numValue}%`;
      });
    });
}

function renderResult(data) {
  console.log("[NewsScope Debug] Full API response:", JSON.stringify(data, null, 2));
  const score = normalizeScore(data.score);
  const deterministicScore = normalizeScore(data.deterministic_score);
  const mlScore =
    data.ml_score === null || data.ml_score === undefined
      ? null
      : normalizeScore(data.ml_score);
  const modules = data.module_scores || {};

  scoreRingFill.classList.remove("high", "medium", "low");
  scoreCircle.classList.remove("tone-high", "tone-medium", "tone-low");
  const scoreClass = getScoreClass(score);
  scoreRingFill.classList.add(scoreClass);
  scoreCircle.classList.add(`tone-${scoreClass}`);
  const ringColor =
    scoreClass === "high"
      ? "var(--success)"
      : scoreClass === "medium"
        ? "var(--warning)"
        : "var(--danger)";
  scoreCircle.style.setProperty("--ring-color", ringColor);
  animateScore(score);

  const labelText = data.label || "Analysis Complete";
  const verdictReason = data.label_reason || "Credibility scan completed.";

  resultLabel.textContent = verdictReason;

  resultBadge.textContent = labelText;
  resultBadge.classList.remove("fake", "original", "verify");
  if (labelText.toLowerCase().includes("fake")) {
    resultBadge.classList.add("fake");
  } else if (labelText.toLowerCase().includes("verify")) {
    resultBadge.classList.add("verify");
  } else {
    resultBadge.classList.add("original");
  }

  const processingTime = data.processing_ms ?? 0;
  const metaParts = [
    `Processing time ${processingTime}ms`,
    `${Object.keys(modules).length} module signals reviewed`,
    `Deterministic ${deterministicScore.toFixed(0)}/100`,
  ];
  if (mlScore !== null) {
    metaParts.push(`ML ${mlScore.toFixed(0)}/100`);
  }
  resultMeta.textContent = metaParts.join(" • ");

  buildInsights(modules, score);
  if (mlScore !== null) {
    resultInsights.appendChild(createInsightCard("ML Model", `${mlScore.toFixed(0)}/100`, getScoreClass(mlScore)));
  }
  resultInsights.appendChild(
    createInsightCard("Deterministic Core", `${deterministicScore.toFixed(0)}/100`, getScoreClass(deterministicScore)),
  );
  renderModules(modules);
  renderExplanations(data.explanations || []);

  emptyState.classList.add("hidden");
  resultBox.classList.remove("hidden");
}

function setInputMode(mode) {
  const isText = mode === "text";
  activeInputMode = isText ? "text" : "link";

  tabTextBtn.classList.toggle("active", isText);
  tabTextBtn.setAttribute("aria-selected", isText ? "true" : "false");
  panelText.classList.toggle("active", isText);
  panelText.hidden = !isText;

  tabLinkBtn.classList.toggle("active", !isText);
  tabLinkBtn.setAttribute("aria-selected", !isText ? "true" : "false");
  panelLink.classList.toggle("active", !isText);
  panelLink.hidden = isText;

  if (isText) {
    textInput.focus();
  } else {
    urlInput.focus();
  }
}

tabTextBtn.addEventListener("click", () => setInputMode("text"));
tabLinkBtn.addEventListener("click", () => setInputMode("link"));

async function pollJob(jobId) {
  const pollIntervalMs = 650;
  while (true) {
    const res = await fetch(`/api/jobs/${jobId}`);
    const data = await res.json();

    if (!res.ok) {
      showStatus(`Error: ${data.error || "Unable to read job status."}`, "error");
      submitBtn.disabled = false;
      submitBtn.innerHTML = '<span class="btn-icon">Analyze</span>';
      return;
    }

    if (data.status === "queued") {
      showStatus(`Queued for analysis. Job ${jobId} is waiting for a worker.`);
      await new Promise((resolve) => setTimeout(resolve, pollIntervalMs));
      continue;
    }

    if (data.status === "processing") {
      showStatus(`Processing article signals. Job ${jobId} is running now.`);
      await new Promise((resolve) => setTimeout(resolve, pollIntervalMs));
      continue;
    }

    if (data.status === "failed") {
      showStatus(`Analysis failed: ${data.error || "unknown error"}`, "error");
      submitBtn.disabled = false;
      submitBtn.innerHTML = '<span class="btn-icon">Analyze</span>';
      return;
    }

    showStatus("Analysis complete.", "done");
    renderResult(data);
    submitBtn.disabled = false;
    submitBtn.innerHTML = '<span class="btn-icon">Analyze</span>';
    return;
  }
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  clearResult();

  const text = textInput.value.trim();
  const url = urlInput.value.trim();
  const source = sourceInput.value.trim();

  if (activeInputMode === "text") {
    if (!text) {
      showStatus("Please enter article text to analyze.", "error");
      return;
    }
  } else {
    if (!url) {
      showStatus("Please enter a URL to analyze.", "error");
      return;
    }
    try {
      const parsed = new URL(url);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        showStatus("Only http/https links are supported.", "error");
        return;
      }
    } catch (_) {
      showStatus("Please enter a valid URL.", "error");
      return;
    }
  }

  submitBtn.disabled = true;
  submitBtn.innerHTML = '<span class="btn-icon">Working</span>';
  showStatus(activeInputMode === "link"
    ? "Fetching article from link and starting analysis..."
    : "Submitting article for analysis...");

  try {
    const payload = activeInputMode === "link"
      ? { text: "", url, source }
      : { text, url: "", source };

    const res = await fetch("/api/jobs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    const data = await res.json();
    if (!res.ok) {
      showStatus(`Error: ${data.error || "Failed to submit."}`, "error");
      submitBtn.disabled = false;
      submitBtn.innerHTML = '<span class="btn-icon">Analyze</span>';
      return;
    }

    showStatus("Article submitted. Starting background analysis...");
    pollJob(data.job_id);
  } catch (err) {
    showStatus(`Network error: ${err.message}`, "error");
    submitBtn.disabled = false;
    submitBtn.innerHTML = '<span class="btn-icon">Analyze</span>';
  }
});

textInput.addEventListener("keydown", (event) => {
  if (event.ctrlKey && event.key === "Enter") {
    event.preventDefault();
    form.dispatchEvent(new Event("submit", { cancelable: true }));
  }
});

urlInput.addEventListener("keydown", (event) => {
  if (event.ctrlKey && event.key === "Enter") {
    event.preventDefault();
    form.dispatchEvent(new Event("submit", { cancelable: true }));
  }
});
