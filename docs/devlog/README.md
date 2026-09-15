# Development log

Narrative log of the `feat/interactive-app` work: what was built each wave,
which defects were found and how, and what the agent orchestration got right
and wrong.

## STALE — read this first

`agent_army_log.html` covers **Waves 0-3 of the app track only**. It does not cover Wave 4
onward, the GUI redesign (Phases 1-6), the v1.8.0 round of fixes, or any of the v2 optical
simulator work. It has not been regenerated since 2026-09-09.

The current record of what was built, what broke and what was measured is:
- `CLAUDE.md` at the repo root — status, open bugs, and the lessons that cost real time.
- `docs/plans/v2_optical_simulator.md` — the v2 plan, eight waves.
- `docs/plans/v2_wave2_sources_design.md` — the detailed Wave 2 design.
- the git log, which carries the reasoning per change.

Treat this log as a historical document, not as the current state.

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
