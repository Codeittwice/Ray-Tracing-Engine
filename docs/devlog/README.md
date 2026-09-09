# Development log

Narrative log of the `feat/interactive-app` work: what was built each wave,
which defects were found and how, and what the agent orchestration got right
and wrong.

- `agent_army_log.html` — the source. Edit this.
- `agent_army_log.pdf` — rendered, A4.

## Regenerating the PDF

No LaTeX or Python PDF library is required; headless Chrome renders the HTML:

```bash
chrome --headless --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="docs/devlog/agent_army_log.pdf" \
  "file:///C:/dev/solar-cooker-rt/docs/devlog/agent_army_log.html"
```

On Windows, Chrome is typically at
`C:\Program Files\Google\Chrome\Application\chrome.exe`. Edge works too via
`msedge.exe` with the same flags.
