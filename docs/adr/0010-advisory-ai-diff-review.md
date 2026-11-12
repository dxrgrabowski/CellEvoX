# ADR-0010: Advisory AI diff review on pull requests
Status: Proposed · 2026-11-11

## Context
Review attention, not test coverage, is the bottleneck on this repository. The
C++ suite pins determinism, event semantics and the snapshot ABI, but it cannot
see a change that is internally consistent and still wrong for the caller: a
step that now returns early, a config field that is written and never read, a
frontend call site left on the old shape. Those are exactly the findings a
reviewer produces by reading the diff against `AGENTS.md`.

Automating a first pass has one hard constraint: CI runs on GitHub-hosted
runners, so there is no local model and no GPU. Any inference over the diff has
to leave the machine, which means the diff, the surrounding file bodies and the
PR body are sent to a third-party provider. That egress, and the cost that comes
with it, is the decision this ADR records.

## Decision
`tools/ai_review.py`, invoked by `.github/workflows/ai-review.yml` on
non-draft pull requests opened from this repository, sends one request per run
to a hosted model and posts the result as a single sticky PR comment.

- **Advisory only.** The workflow never approves, never requests changes and is
  not a required check. The comment ends with `_Advisory: findings do not block
  the merge._`. A model or API failure fails the job so it is visible, but the
  comment is still written with an explicit "did not produce a result" body that
  quotes the tool's own last stderr line, so an over-budget payload reads
  differently from a provider failure. A run that fails after writing
  `comment.md` does not publish that file: the fallback wins unless the step
  succeeded.
- **One sticky comment.** The prior comment is found by bot identity plus a body
  that *starts with* the `<!-- cellevox-ai-review -->` marker followed by this
  review's heading, not by login, so a token change cannot start a second comment
  on every push, and a bot comment that merely quotes the marker or opens with the
  marker alone is not rewritten as ours. With `--repeat`, the
  published run is the upper middle of the successful runs by finding count (the
  higher of the two middle runs when the count is even), so a failed last call
  cannot turn a run with findings into "No findings". Each published finding
  carries `support=k/N`, where N is the calls that returned, so a failed call is
  not counted as a vote against a finding every successful call reported, and a
  single-run outlier is not shown as if it were stable. A finding a strict
  majority of the calls that returned reported is also published when the
  published run missed it, so which run happened to be chosen cannot hide a
  stable finding; exactly half of an even count is not a majority and stays out.
  The published run is chosen once and supplies the comment's dropped count. A
  majority finding that run missed is added back, so the findings shown can
  include one the published run did not report; it carries its own
  `support=k/N` rather than the published run's, which is what tells the two
  apart. A `--md-out` file from an earlier invocation is removed at startup, so no
  exit path that says "no result" can leave a stale comment behind for a consumer
  to publish; a successful run writes the file anew (the `-repeatN.json` summary
  is still written on an all-failed run, because it is debugging output for that
  failure, not a result). The
  body is model text rendered for humans, so
  mentions in it are neutralized (`@` -> `@` followed by a zero-width space) and
  the evidence fence is longer than any backtick run inside the quote, so a diff
  line the author controls cannot ping a user or close the code block.
- **Bounded egress.** The payload is the unified diff, the full body of changed
  files in `BODY_EXTENSIONS` (capped at `MAX_BODY_CHARS` each), the PR body, the
  `AGENTS.md` invariants for the changed paths and the test inventory (Catch2
  cases and pytest test functions). A diff touching only `.github/`, `tools/` or
  `docs/` matches no numbered invariant, so it is given a short infrastructure
  scope instead of "none matched": a secret reachable from code or a third-party
  action, a mutable action pin, a failure path that fails open, and a result
  dropped without a trace. That block is in the citable allow-list as well, so
  citing it is not an invented invariant.
  Nothing else from the checkout is read, and none of the application code is
  executed: the tool reads the diff and file bodies, it does not build or run the
  project. The tool itself is PR code — a `pull_request` run uses the head
  branch's files — which is why the job is limited to same-repository PRs; a fork
  PR would run code its author controls, and it is skipped at job level. No
  other secret is exposed to the step. The provider key travels in a request
  header, never as a `?key=` parameter: a URL is copied into proxy logs and into
  the provider's own error text, and the key is a secret.
- **Provider and model.** `deepseek/deepseek-v4.1-flash-1017` through OpenRouter,
  `temperature 0`, reasoning disabled, one call unless `--repeat` is passed.
  `--provider` and `--model` stay overridable for local runs. Rate-limit and
  server errors are retried with backoff, and so are connection failures and
  read timeouts: those are transient, and letting them escape as a traceback
  would skip the backoff and leave the workflow quoting a stack trace instead of
  a reason. A 200 whose body is not JSON (a proxy error page, a truncated
  stream) is not retried: it is a reply of an unexpected shape and exits with a
  one-line reason, like a missing `choices` or an empty answer. A retried error
  exits with a one-line reason on the last attempt.
- **Grounded findings.** The model must quote the changed line it is talking
  about. The line number is derived from that quote's position in the diff, never
  taken from the answer, and a finding whose quote is not in the diff is dropped
  and listed as dropped instead of being shown. A finding whose own reasoning
  concludes there is nothing to report is dropped as `self-refuted`, so the model
  cannot publish its own refutation as a finding; a contrast marker after the
  refutation (`consistent, but ...`) keeps a claim that is still live. A quote
  that also matches an
  unchanged context line resolves to the added or removed line, so it cannot be
  pinned to a line the PR did not touch. The deterministic test inventory
  and invariant list are injected by path prefix, not guessed by the model; the
  allow-list of citable invariants is the set the payload builder actually
  injected, so the two cannot disagree about what the model saw.
- **One prompt.** The prompt is a single `SYSTEM` constant. There is no variant
  flag and no per-call prompt selection.
- **Second arm for comparison.** `.github/workflows/robin.yml` runs the
  third-party `antongulin/robin` action, pinned to its `v2.13.3` commit, on the
  same events and on the same model, keyed from the same
  `OPENROUTER_API_KEY`. It is given `AGENTS.md` through
  `review-instructions-file`, read from the PR base branch, because single-shot
  mode has no tools: naming the file in the inline text does not make the action
  read it, and the contract has to be in the prompt. The tool selects invariants
  by path while Robin receives the whole file, so this narrows the difference
  between the arms without removing it. It is configured advisory
  (`request-changes: false`),
  single-shot (`agent-mode: off`), and with a 1M-character diff ceiling,
  `temperature 0` and reasoning disabled on both (`reasoning: {enabled: false}`
  for the tool, OpenRouter's `effort: "none"` for Robin), so a difference
  between the two reviews comes from the
  reviewer rather than from the model or the trigger. The ceilings are the same
  1M characters but bound different quantities: this tool's `--max-chars` bounds
  the assembled payload (diff, file bodies, inventory), Robin's bounds the diff
  alone, and Robin refuses an oversized diff rather than truncating it. Neither
  ceiling contains the other, so on a PR near the limit one arm can refuse while
  the other reviews; the workflow does not override `--max-chars`, so a local run
  and CI share one budget. Robin posts its own
  review with inline comments; there is no scoring harness in this ADR, the two
  reviews are read side by side.
- **Merge-safety.** Fork PRs are skipped by a job-level `if` because they receive
  no secrets; drafts are skipped because the body and diff are still moving;
  `concurrency` cancels an in-flight review when the branch is pushed again. The
  upsert is guarded by `!cancelled()` rather than `always()`, because `always()`
  is true on cancellation too and the cancelled runs of this workflow have
  executed the post step. Cancellation alone does not make the ordering safe: a
  run that finished after a newer push is not cancelled, so the script compares
  the event's head with the current PR head and writes nothing when they differ
  (a re-run of an old commit is the same case). The run for the newer commit
  owns the marker-keyed comment and rewrites it. Every run that is still the head
  updates the comment, including when the model call fails; a cancelled run is
  neutral rather than red, since it produced no result by definition. So is a run
  the head comparison superseded: the post step publishes that outcome and the
  failure step reads it, because a superseded run is not always a cancelled one.

## Alternatives considered
- **A blocking review gate.** Rejected: the model is non-deterministic and has no
  access to runtime evidence, so a false finding would block a correct PR. The
  workflow can be promoted to a required check only if a benchmark shows a
  false-positive rate low enough to justify it, which this ADR does not claim.
- **A local model on a self-hosted runner.** Rejected for now: it removes the
  egress but adds a GPU runner and a model-upgrade path, and the diff does not
  leave the repository only if the runner is trusted with the checkout. Worth
  revisiting if egress becomes restricted.
- **Replacing this reviewer with an off-the-shelf bot.** Not adopted. Robin runs
  alongside as a comparison arm (see Decision), but on its own it cannot select
  the `AGENTS.md` invariants by path (it is handed the whole file), does not
  verify that a finding quotes a
  line that exists in the diff, and posts in its own format. CodeRabbit is
  installed as a GitHub App outside the workflow, so it cannot be pinned in the
  repository or produce a CI signal; PR-Agent was considered and is not used.
- **A GitHub review with inline comments instead of one comment.** Rejected for
  now: inline comments need a diff-position mapping that goes stale on every
  rebase, and the sticky-comment model keeps the review to one editable place.
- **No automation.** Rejected: the class of finding this targets is missed today,
  and the review is advisory, so the cost of being wrong is one comment.

## Consequences
- Every non-draft, same-repository PR sends code and PR prose to OpenRouter.
  There is no per-PR opt-out other than marking the PR draft or removing the
  workflow; a private fork of this repository inherits that behaviour.
- Cost scales with diff size and is not capped by the workflow. The payload cap
  (`--max-chars`) and `--max-changed-lines` bound it, but a large PR still costs
  more than a small one, and nothing stops a series of large PRs.
- File bodies are bounded twice: a body above `MAX_BODY_CHARS` is skipped, and
  `--max-chars` drops the largest remaining bodies until the payload fits. When
  no readable body survives (every changed file is too large, or
  `--max-changed-lines` is below the diff size), the review is *diff-only*: it
  sees the changed lines but not the files around them. A blocker claims
  knowledge that view cannot give, so every blocker is published as a warning
  with `DOWNGRADED_DIFF_ONLY`, the header says `diff-only`, and the skipped
  bodies are listed by reason. `--max-changed-lines` defaults to 0 (no line cap),
  so `--max-chars` is the single budget a local run and CI share.
- The review needs `OPENROUTER_API_KEY` in repository secrets. Missing or expired
  keys surface as a failed job and a fallback comment, not a silent pass.
- Changing the model is a one-line default plus a workflow line; the JSON schema,
  quote verification and rendering are model-independent.
- Findings are advisory text. Nothing in this ADR makes the comment part of the
  merge contract, and the PR body's `Verified commit` still refers to human and
  CI evidence.
- Two reviews now land on every non-draft PR: one sticky issue comment from this
  tool, one review with inline comments from Robin. Each is rewritten by its own
  workflow, and removing either means deleting its workflow file; there is no
  toggle that disables one arm. They also cancel independently, so during a burst
  of pushes one arm can finish on an earlier commit than the other; the two
  comments are compared once both have settled on the head commit.
- Robin is a third-party action in the supply chain. It is pinned to the
  `v2.13.3` commit rather than a branch, so an upstream change cannot alter what
  runs here, but an update is a manual edit that this repository's CI does not
  exercise.
- Robin shares `OPENROUTER_API_KEY` with this tool, so a revoked or expired key
  disables both reviews at once. Robin publishes no token counts, so its cost is
  visible only in the OpenRouter dashboard, not in the run.
- The tool is stdlib-only and pinned to this repository by design: it reads
  `AGENTS.md`, the `INVARIANTS_FOR` path map and the Catch2 and pytest test
  trees. (Its tests
  use pytest, like the other test jobs; the claim covers the tool.) It is not
  a reusable action until a second repository needs it.

## Evidence
- `tools/test_ai_review.py` — unit tests over diff parsing (including the
  `--- `/`+++ ` content-line case), quote verification and drop reporting, a
  finding that refutes itself dropped as `self-refuted` while a refutation
  followed by a contrast keeps its live claim, a "not itself a finding"
  phrasing dropped even when a contrast follows it, and a "Not a silent drop."
  tail dropped as the same refutation, pytest names in the inventory,
  the infrastructure scope for a `.github/`-only diff, a zero line cap still
  sending bodies, a body above `MAX_BODY_CHARS` making the review diff-only,
  a blocker downgraded to a warning in that mode,
  changed-line preference over an identical context line, longest-prefix
  invariant selection, the `INVARIANTS_FOR` map checked against `AGENTS.md`'s
  numbering so a renumbering fails CI, unconditional invariant/inventory/file-body
  context, the over-budget refusal, largest-body-first trimming that leaves the
  smaller body in the payload, head-versus-range rejection, comment
  rendering including a partial-failure header, skipped-versus-trimmed
  bodies reported on separate lines, mention neutralization and a fence that a
  backtick run cannot close, upper-middle run selection under a
  failed final call, a tie and an even
  run count, a majority finding the chosen run missed being added back at its
  own support, a tied even count staying out because the bar is a strict
  majority, support counts across runs with a failed call excluded from the
  denominator, an
  end-to-end CLI run from a saved response that lands `support=k/N` in the
  comment, an unreadable `--response-file` reported as a reason, an all-failed
  repeat run and a single unparseable run that write no comment and remove a
  stale one, the provider call
  path with `urlopen` stubbed — the Google key in a header rather than the URL,
  a malformed reply (including a non-JSON 200 body) and an empty Google answer
  reported as a one-line reason
  rather than a traceback, and a transient network error retried and then
  reported as a reason — and the
  workflow
  wiring: the Robin pin and its OpenRouter reasoning setting, every passed input
  checked against the action's
  declared set, the fallback reason including the empty-stderr message, no
  `--max-chars` override so CI and a local run share the tool's default, the step timeout,
  the `!cancelled()` guard plus the head comparison that keep a superseded run
  from overwriting the comment the newer run owns, the failure step that stays
  neutral on cancellation and on the supersession the post step publishes, both
  workflows using the tool's default model, the
  invariant map pinned on representative paths, the Robin instruction file, and the
  bot-identity lookup keyed on the marker plus the review heading opening the
  body. Run by the
  `review-tools` job in `.github/workflows/ci.yml` with no network access.
- Dry run on this branch (`--dry-run`, `origin/main...HEAD`): 8 files, ~2.9k
  changed lines, a payload of ~155k characters (~39k tokens; the real run is
  larger because the PR body is the intent), ~120 tests in the inventory, the
  infrastructure scope injected, and `diff-only` reported because both readable
  changed files (`tools/ai_review.py`, `tools/test_ai_review.py`) are above
  `MAX_BODY_CHARS`. The figures are rounded because the diff contains this ADR:
  every edit to it moves the measurement, and a test added beside it moves the
  inventory with it. The run needed no key and no network.
- `tools/ai_review.py` (`build_context`, `verify`, `render_markdown`),
  `.github/workflows/ai-review.yml` (upsert script, fallback body) and
  `.github/workflows/robin.yml` (pinned action, advisory and single-shot inputs).
