# Hypercuboid search: findings and open questions

First-result searches and complete enumeration favor different optimizations.
The strongest recent depletion improvement is local search over related points;
paired departures add a smaller, repeatable benefit on a broader corpus.
Percentages below come from separate incremental comparisons against different
baselines and must not be added together. Measurements are local Release
AppleClang runs; parsing and replay are excluded, and allocation diagnostics are
separate from normal timing.

## Recent optimization results

| Change | Main result | Scope and decision |
| --- | --- | --- |
| Consuming point removal plus `integer_set` matching masks | Large-fixture enumeration takes 10.5–16.9% less time; Deferred allocation calls fall 24–28%. | Original four count fixtures; first-result means are roughly unchanged. Keep both. |
| Consuming slice removal | `100timelines` takes about 14–16% less time in primary and confirmation runs; allocation calls fall substantially. | Other first-result changes vary; large-space enumeration is essentially flat. Keep for propagation work. |
| Entry classification masks | Gigantic-fixture workload means fall 7–21%; perftest means fall about 4%. | Complete enumeration is flat. Keep masks; the separate nonjump/deferred-ghost-pruning fast path regressed enumeration by 2–4% and remains disabled. |
| Local physical/arrival search | Complete-count workload means fall about 40–44%; prefix means fall about 32–33%. | Original four count fixtures, including the eight-action NP endpoint. First-result means stay within about 2%; local search starts only after the first yield. |
| Paired departures in local search | Nontrivial complete enumeration takes about 5–6% less time; allocation calls fall about 6–7%. | Confirmed on the expanded corpus below. Keep the extension, with a peak-storage tradeoff. |
| Iterator ordering interface | Complete-enumeration aggregate changes stay within about 1%; gigantic first-result cases take 3–5% less time. | No broad depletion speedup; retain the interface cleanup. Random/scored/weighted full-enumeration probes are also approximately flat. |
| Physical-first opening phase | Deferred perftest takes 2.5% less time across 50 games, and 4.5% less in a longer confirmation; first-10/100-action prefixes take 6.1%/3.1% less time. | Full depletion and capped counts are essentially flat across 275 positions; four gigantic cases are flat in confirmation. Keep the phase. |

The physical-only local phase already reduced complete-count means by 35–41%.
Allowing compatible arrivals supplied a smaller additional gain: sums of case
medians fell another 3–5%. These results compare against a baseline already
containing consuming removals and matching-mask improvements.

The opening phase searches physical moves on every mandatory timeline, with
null entries elsewhere, using only royal-safety checks. Outer subtraction waits
until the phase finishes, avoiding splitting for first-result callers. All 27
Release tests pass, and 290 complete action-set comparisons match the baseline
without duplicates. Percentages for this experiment are geometric means of
per-case median ratios; both binary orders were tested.

Lazy splitting did not establish a broad advantage. The negative-slice selector
comparison was approximately flat; lazy persistent point removal improved the
two large original depletion cases by about 5%, but did not consistently help
first-result workloads or reduce measured peak heap storage. These experiments
do not justify replacing the retained eager representation generally.

## Compared with v0.5.2

The cumulative comparison estimates the current Deferred search, including the
physical-first opening phase, against v0.5.2's Balanced default. It includes the
intervening optimizations and default-policy change. Values are geometric means
of per-case elapsed-time ratios; negative means less time.

| Workload | Cases | Estimated elapsed-time change |
| --- | ---: | ---: |
| Complete enumeration, at least 1,000 actions | 60 | -55.8% |
| Complete enumeration, fewer than 1,000 actions | 179 | -42.4% |
| Capped at 50,001 actions | 36 | -54.8% |
| First 100 actions, limit reached | 106 | -47.7% |
| First 1,000 actions, limit reached | 96 | -53.7% |
| Gigantic first-result cases completing in both versions | 3 | -44.0% |
| Perftest games | 50 | -6.8% |

The estimated sum of all 275 count-case medians falls from 8.90 to 4.13 seconds,
or 53.6%. Weighting perftest by the sum of game medians gives 13.0% less time.
Match 123 does not finish within ten seconds of search in v0.5.2, versus about
12 ms with Deferred; the timeout is excluded from the finite aggregates.

These estimates multiply the recorded release-comparison and physical-first
ratios for each case before aggregation, using the longer first-result
confirmation. They are not a direct final-versus-tag timing run. The underlying
comparisons use the same Release configuration and timing driver, reversing
binary order. The tag core is unchanged; parsing and replay are excluded, while
HC construction and search are timed. Counts and terminal outcomes agree.

## Broader depletion comparison

The fixed corpus contains **275 positions from 86 source games**, including
intermediate positions: 239 fully enumerated spaces of at most 200,000 actions
and 36 larger spaces capped at 50,001 actions. The main complete-enumeration
aggregate uses the **60 positions with at least 1,000 actions**, spanning 45
primary games. The other 179 complete spaces are terminal or small cases.

Relative elapsed-time changes below are geometric means of per-position median
ratios; negative means less time. Paired departures are compared against local
physical/arrival search with fixed departures. Iterator traversal is a separate,
later comparison against the callback interface.

| Change / workload | Positions | Balanced | Stable | Deferred |
| --- | ---: | ---: | ---: | ---: |
| Paired departures: complete, at least 1,000 actions | 60 | -5.3% | -4.9% | -6.1% |
| Paired departures: capped at 50,001 | 36 | +0.3% | +0.4% | -0.1% |
| Iterator traversal: complete, at least 1,000 actions | 60 | +0.6% | +0.4% | +0.0% |
| Iterator traversal: capped at 50,001 | 36 | +0.2% | -0.2% | +0.0% |

The departure gains persist when reversing binary order and excluding the
original fixtures. For each policy, 41 of the 60 nontrivial complete spaces
improve by more than 3%, with none regressing by more than 3%; individual gains
reach roughly 10–16%. The 100- and 1,000-action prefixes are approximately flat.
Intermediate positions from the same game are correlated, and capped searches
do not establish performance for complete depletion of the largest spaces.

Fewer allocations do not imply lower peak storage. Median peak-live-HC increases
are one object for Balanced/Stable and two for Deferred, with larger outliers:
online game 235 at action 39 rises from 39 to 123 live HCs under Balanced.
These counters measure objects, not bytes or process memory.

All 239 complete action sets agree across the paired implementations and three
policies, without duplicates. The iterator comparison additionally preserves
315 case/policy action sequences, using complete enumeration or bounded prefixes.
Individual iterator timing regressions of about 3–4% remain despite flat
aggregates. Later naming and helper-scope cleanups were not separately profiled;
MSVC verification remains for CI.

The maintained configurations select [20 default count positions](../../scripts/profile_hc_games.json)
and [255 expanded positions](../../scripts/profile_hc_expanded.json), referencing
original games with optional submitted-turn cutoffs. See the
[corpus guide](../../test/pgn/count/README.md) and
[profiling commands](../usage/tools.md#hc-search-profiling).

## Ordering and production scope

`HCOrdering` consistently orders coordinates within each axis. The intended
action-level property is weaker than decreasing summed semimove scores: an
action preferred on every axis should precede one it dominates in the product
partial order. The implementation only partially preserves this property.
Selecting the preferred corner and removing individual points respect it within
a rectangle, but problem-slice splitting and stack traversal can reorder the
remaining regions. Matching finds a feasible pairing rather than an optimal
one under this order, and draining a local group also takes priority over
choices elsewhere.

Structural preferences explicitly override coordinate rankings. Previously,
non-branching regions were searched before branching regions even when a
branching semimove ranked higher. The physical-first phase now searches a
non-jump action first even when jump semimoves precede all physical alternatives
on some axes. Matching already preferred physical/null choices when no pairing
was required, so this adds an override rather than breaking a previously
enforced global guarantee.

The current `HC_info::search()` is accepted for production checkmate detection
and counting. Fine-tree search, used by MCTS, needs stronger adherence to the
supplied ordering; reviewing and improving that behavior is deferred to later
work. Current throughput results do not establish MCTS search quality.

## Earlier propagation findings

- **Preserve global conflicts.** Clipping problems to the current HC loses
  reusable constraints and causes repeated rediscovery. `take_point` enforces
  matching; Present advancement and royal safety are separate constraints.
- **Match 123 has a short contradiction.** Full propagation discovers one Present
  problem and three royal-safety problems, then rejects 663 further HCs through
  matching. Exhaustion took about 34 ms. The Present slice has universe
  codimension 14 but relative codimension one in its originating HC; propagation
  expands 58 HCs to 721.
- **Global propagation has a fragmentation cost.** It resolves unrestricted
  match 123, but match 72's fine-tree normalization grows from about 12 to
  296 ms. Removing only the non-intersection stop does not resolve match 123;
  codimension is the stronger variable in those experiments.
- **Reuse owned storage.** In-place region replacement reduced match 72's
  full-propagation search time from about 40 to 18 ms. Earlier singleton-skipping
  and final-piece reuse in point removal reduced the original 30,134- and
  55,332-action case times by 13–15% and 30–31%, respectively.
- **First-result bottlenecks differ.** Matching-first Deferred reduced match 59
  from 21.3 to 3.9 ms and match 123 from 34.5 to 15.6 ms in one historical
  comparison, without a consistent typical-position gain. Weighted preparation
  on `100timelines` separately cost about 76 ms for scoring and 377 ms for
  ordering, versus roughly 4 ms for the subsequent search.

Deferred retains global problem slices, propagates universe-codimension-zero/one
problems immediately, and propagates a smallest-codimension slice when negatives
outnumber positive HCs. Relative-codimension-zero/one removals are also persisted
in each visited HC, repeating to a fixed point. Matching is attempted before
temporary negative-slice traversal. These rules avoid some repeated discovery
and splitting, but the negative/positive count gauge remains heuristic.

![Problem and relative codimension distributions for match 123 and 100timelines](res/hc-codimension-histograms/codimension-histograms.png)

Relative codimension follows `is_slice_good()`. Secondary histograms count
intersecting slice–HC pairs, excluding disjoint pairs, on a logarithmic axis.
Match 123's 1,438 secondary pairs arise from only four problems; all 12 problems
in `100timelines` have universe codimension one. These trajectory-dependent
samples do not establish an optimal policy.

Historical supporting measurements: [core/fine-tree comparisons](res/hc-design-profile/),
[four-policy extension](res/hc-design-profile-allgood/), and
[histogram counts](res/hc-codimension-histograms/histograms.csv).

## Remaining questions

- Should propagation use removal volume or recent slice quality in addition to
  codimension and positive/negative counts?
- Can the larger peak-storage outliers be reduced without losing depletion gains?
- Can fixed-history attacks be recognized earlier? The three unary safety
  constraints in match 123 attack historical kings; earlier validation was not
  evaluated in these measurements.
