# Data Quality Expansion Prompt (Reusable)

Use this prompt with any data-generation assistant to expand NewsScope datasets in a structured, high-quality way.

```text
You are a data curator for a deterministic news credibility system (no ML).

I need high-quality, diverse, non-duplicative data files for three resources:

1) sources.csv
2) suspicious_phrases.txt
3) negative_terms.csv

Output requirements:
- Return ONLY valid file contents for each file.
- Use plain UTF-8 text, no markdown formatting.
- Keep content realistic, domain-diverse, and balanced.
- Avoid profanity/extreme harmful content.
- Avoid exact duplicates and near-duplicates.

File schemas:

[sources.csv]
Header: source,status,credibility
- status must be one of: trusted,neutral,untrusted
- credibility must be integer 0-100
- Include at least:
  - 120 trusted
  - 80 neutral
  - 80 untrusted
- Cover regions: global, US, EU, India, APAC, Africa, LATAM
- Cover categories: mainstream, local, niche blogs, satirical-looking names, rumor-style outlets
- Ensure naming consistency (no casing duplicates)

[suspicious_phrases.txt]
- One phrase per line
- 500 unique phrases
- Mix lengths: 2-6 words
- Include categories:
  - clickbait hooks
  - urgency manipulation
  - unverifiable sourcing language
  - conspiracy framing
  - “too good to be true” language
- Keep all phrases lowercase
- Exclude obvious legal/medical misinformation claims that could be harmful if reused verbatim

[negative_terms.csv]
Header: term,weight
- weight in [0.10, 0.95]
- 400 unique terms/short phrases
- Prefer single terms and short bigrams
- Higher weights for stronger misinformation cues (e.g., hoax-like framing)
- Lower weights for ambiguous terms (e.g., rumor-like words)
- Keep lowercase

Quality rules:
- No placeholder text (no lorem ipsum)
- No repeated lines
- No trailing spaces
- Deterministic ordering:
  - sources.csv sorted by status then source name
  - suspicious_phrases.txt alphabetically
  - negative_terms.csv by descending weight, then term

Finally, include a short QC summary:
- total rows/lines per file
- min/max/mean credibility for each source status
- min/max/mean weight for negative_terms.csv
- duplicate count detected (should be 0)
```
