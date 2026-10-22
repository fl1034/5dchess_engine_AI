# Hypercuboid search: findings and open questions

The experiments use matches 59, 72, and 123 in `test/pgn/slow/`, plus
`test/pgn/100timelines.5dpgn`. These positions may be strongly biased. Timings
refer to the experimental policies before the latest 100-problem fallback changes.

## Findings

1. **Preserve global conflicts.** HC-local problem construction clips reusable
   constraints and causes repeated rediscovery. `take_point()` enforces matching,
   not Present advancement or royal safety; no matching correctness defect was
   identified here.

2. **Match 123 has a short contradiction.** Full-removal stable search discovers
   one Present constraint and three royal-safety constraints. After propagation,
   663 further HCs produce no matching point. Exhaustion takes about 34 ms and
   the position is checkmate.

3. **One slice causes the fragmentation burst.** The Present problem has
   universe codimension 14 but relative codimension 1 in its originating HC.
   Propagation expands **58 HCs to 721**. All 12 problems in `100timelines`
   have universe codimension 1, with no intersecting target exceeding 1.

4. **The three unary safety constraints attack historical kings.** The attacks
   are knight `(23T15)g6 → (21T15)g5`, bishop `(-11T16)g7 → (-7T16)g3`, and
   bishop `(3T18)f8 → (3T11)f1`. Targets and intermediate boards are fixed
   history, so other action coordinates cannot repair them. Eager pruning or
   cached lazy validation could detect them earlier; neither has been evaluated.

5. **Full propagation has substantial tradeoffs.** It resolves unrestricted
   match 123 but fragments other spaces. On match 72, fine-tree normalization
   alone takes about **296 ms versus 12 ms** with selective propagation.

6. **Codimension is the stronger policy variable here.** Scanning every HC
   while retaining codimension ≤1 gives similar completed-case timings and
   still fails on unrestricted match 123. Disabling the non-intersection stop
   alone does not solve that case.

7. **Replace regions in place.** With identical full-propagation search-work
   counts, in-place removal takes about **18 versus 40 ms** on match 72.
   Replacing B in A, B, C must keep B's pieces between A and C.

8. **Weighted ordering has a separate bottleneck.** On `100timelines`, coordinate
   scoring takes about **76 ms**, weighted-order construction **377 ms**, and
   subsequent matching, validation, and removal about **4 ms** when propagating
   from the first problem.

9. **Avoid redundant terminal searches.** After proving no legal action exists,
   the phantom-check test distinguishes checkmate from stalemate without
   repeating `get_mate_type()`'s legality searches.

## Representative timings

Median milliseconds, three trials. Old uses local slices; other columns use
global slices. Secondary cuts use codimension ≤1 in both
“Global” columns; “all” disables the density stop. “Full” has no codimension
limit. The selected HC is always cut. Core excludes setup; fine-tree includes
root setup and excludes destruction. Ordering is natural unless marked random
(seed 42). `>` denotes a timeout.

| Position / operation | Old | Global + stop | Global ≤1, all | Full |
| --- | ---: | ---: | ---: | ---: |
| Match 123, core | >5000 | >5000 | >5000 | 34.3 |
| Match 123, restricted partition, core | >5000 | 3.5 | 3.5 | 277.2 |
| Match 72, core | 1.2 | 2.1 | 2.1 | 40.6 |
| Match 72, fine-tree | 14.8 | 15.7 | 15.9 | 340.4 |
| Match 59, random fine-tree | 1234.1 | 53.6 | 59.9 | 13.6 |
| 100timelines, core | 11.0 | 11.3 | 11.4 | 11.3 |
| 100timelines, fine-tree | 19.7 | 19.7 | 20.0 | 20.6 |

The restricted partition is an intermediate `get_mate_type()` search on the
same position; exhausting it alone does not prove checkmate.

## Codimension histograms

![Problem and relative codimension distributions for match 123 and 100timelines](res/hc-codimension-histograms/codimension-histograms.png)

Relative codimension follows `is_slice_good()`. Secondary histograms count
intersecting slice–HC pairs, excluding disjoint pairs, on a logarithmic axis.
Match 123's **1,438 secondary pairs come from four problems**, not 1,438
independent problems. These small, trajectory-dependent samples differ mainly
in the expensive tail.

## Open question

How should recent slice-quality observations and space size or memory control
codimension relaxation? Higher limits expose constraints but multiply fragments;
a codimension-2 propagation pass can potentially double HC count. Retained
problems may become cheaper as HCs shrink. The history statistic, gauge, and
relaxation schedule remain open mathematical questions.

Small action-set comparisons and native tests passed; they do not establish
an optimal policy. Raw data: [comparative profiles](res/hc-design-profile/),
[four-policy extension](res/hc-design-profile-allgood/), and
[histograms](res/hc-codimension-histograms/histograms.csv).
