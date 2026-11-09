<!-- Answer every section that applies. What changed and why, Acceptance criteria,
     and Verification are required; delete the other two with their comments when
     they do not apply. The automated check reads the headings, the two
     verification labels, and whether an answer is present; it cannot tell whether
     the answer is true, whether the user approved it, or whether the change
     works. The rules behind the sections are in AGENTS.md. -->

## What changed and why

<!-- Observable before → after: behaviour, names, formats, exit codes. For a
     docs/CI-only change, say what changes and that runtime behaviour does not.
     State the requirement that asked for it: the task or comment, quoted or
     summarized when it came from a conversation, and separate what the user
     asked for from what you inferred. A link is not required when no durable
     one exists. -->

## Decisions and open questions

<!-- Only what a reviewer needs to judge the change; delete the section when
     there is nothing to say. Say which each item is: a decision you made within
     your mandate, a proposal put to the user, an explicit user approval, a
     question still open, or a problem the user was told about but has not
     answered. Silence, or a general "open the PR and merge", is not approval.
     For a non-obvious choice — a new dependency, a format, an architectural
     trade-off — name the rejected options and the constraint that ruled them
     out; an obvious fix needs no invented alternatives. -->

## Scope and constraints

<!-- What stays outside the task, and which properties must keep working. Omit
     when the task itself says this; do not invent limits or repeat every
     invariant. Name any Hard Domain Invariant the diff could break. This text
     guides review — it does not replace the user's requirements or AGENTS.md. -->

## Acceptance criteria

<!-- Observable pass/fail conditions, including behaviour that must stay
     unchanged when the change touches shared code, outputs, or formats. If an
     agreed criterion or constraint changes later, say what changed, why, and
     which agreement it rests on. A change with no observable behaviour — a docs
     or comment-only change — says `N/A` and why. -->

## Verification

Verified commit:

<!-- What was checked, against which version, with what result, and what was not
     checked. Quote the decisive command output, not the whole log. An empty code
     block or a bare "tests pass" proves nothing; a check you did not run is
     declared, not implied. Completeness here does not prove the change is
     correct — the reviewer still judges the criteria and the evidence. -->

Not checked:
