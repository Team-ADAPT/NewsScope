const form = document.getElementById("analyze-form");
const submitBtn = document.getElementById("submit-btn");
const statusBox = document.getElementById("status-box");
const resultBox = document.getElementById("result-box");

const resultLabel = document.getElementById("result-label");
const resultScore = document.getElementById("result-score");
const resultBadge = document.getElementById("result-badge");
const resultMeta = document.getElementById("result-meta");
const moduleScores = document.getElementById("module-scores");
const explanations = document.getElementById("explanations");

function showStatus(message) {
  statusBox.textContent = message;
  statusBox.classList.remove("hidden");
}

function clearResult() {
  resultBox.classList.add("hidden");
  moduleScores.innerHTML = "";
  explanations.innerHTML = "";
}

function renderResult(data) {
  const score = Number(data.score || 0);
  resultLabel.textContent = data.label || "Result";
  resultScore.textContent = `Credibility Score: ${score.toFixed(2)} / 100`;
  const verdictReason = data.label_reason ? `Verdict: ${data.label_reason}` : "";
  resultMeta.textContent = verdictReason
    ? `${verdictReason} • Background processing time: ${data.processing_ms ?? 0} ms`
    : `Background processing time: ${data.processing_ms ?? 0} ms`;

  resultBadge.textContent = data.label || "Done";
  resultBadge.classList.remove("fake", "original", "verify");
  if ((data.label || "").toLowerCase().includes("fake")) {
    resultBadge.classList.add("fake");
  } else if ((data.label || "").toLowerCase().includes("verify")) {
    resultBadge.classList.add("verify");
  } else {
    resultBadge.classList.add("original");
  }

  const modules = data.module_scores || {};
  Object.entries(modules)
    .sort((a, b) => a[0].localeCompare(b[0]))
    .forEach(([name, value]) => {
      const li = document.createElement("li");
      li.textContent = `${name}: ${Number(value).toFixed(2)} / 100`;
      moduleScores.appendChild(li);
    });

  (data.explanations || []).forEach((line) => {
    const li = document.createElement("li");
    li.textContent = line;
    explanations.appendChild(li);
  });

  resultBox.classList.remove("hidden");
}

async function pollJob(jobId) {
  const pollIntervalMs = 800;
  while (true) {
    const res = await fetch(`/api/jobs/${jobId}`);
    const data = await res.json();

    if (!res.ok) {
      showStatus(`Error: ${data.error || "Unable to read job status."}`);
      submitBtn.disabled = false;
      submitBtn.textContent = "Analyze in Background";
      return;
    }

    if (data.status === "queued" || data.status === "processing") {
      showStatus(`Job ${jobId}: ${data.status}...`);
      await new Promise((resolve) => setTimeout(resolve, pollIntervalMs));
      continue;
    }

    if (data.status === "failed") {
      showStatus(`Job failed: ${data.error || "unknown error"}`);
      submitBtn.disabled = false;
      submitBtn.textContent = "Analyze in Background";
      return;
    }

    statusBox.classList.add("hidden");
    renderResult(data);
    submitBtn.disabled = false;
    submitBtn.textContent = "Analyze in Background";
    return;
  }
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  clearResult();

  const text = document.getElementById("text-input").value.trim();
  const source = document.getElementById("source-input").value.trim();

  if (!text) {
    showStatus("Please enter text to analyze.");
    return;
  }

  submitBtn.disabled = true;
  submitBtn.textContent = "Queued...";
  showStatus("Submitting background job...");

  const res = await fetch("/api/jobs", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ text, source }),
  });

  const data = await res.json();
  if (!res.ok) {
    showStatus(`Error: ${data.error || "Failed to submit job."}`);
    submitBtn.disabled = false;
    submitBtn.textContent = "Analyze in Background";
    return;
  }

  showStatus(`Job ${data.job_id} submitted. Processing in background...`);
  submitBtn.textContent = "Processing...";
  pollJob(data.job_id);
});
