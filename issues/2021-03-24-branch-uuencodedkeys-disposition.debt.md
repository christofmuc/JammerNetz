# Title
Assess `uuencodedKeys` crypto-key loading branch

## Status
open

## Priority
P2

## Area
security/configuration/protocol

## Summary
`uuencodedKeys` adds support for loading crypto keys from base64 strings instead of binary files, but remains unmerged and undocumented in mainline.

## Evidence
- Branch: `origin/uuencodedKeys`
- Divergence vs `origin/master`: ahead `1`, behind `383`
- PR state: none found
- Commit message indicates feature value but incomplete documentation.

## Impact
Could improve key distribution/operations, but requires careful security and compatibility review.

## Hypotheses
- Feature may still be valuable if implemented with explicit input validation and docs.

## Reproduction
1. Inspect commit: `git show --stat origin/uuencodedKeys`.
2. Review security implications for key handling paths in client/server/common.

## Next steps
1. Decide whether text-encoded key input is a supported product requirement.
2. If yes, reimplement on current master with tests and docs.
3. If no, close branch and record rationale.

## Exit criteria
- Decision captured; feature either scheduled with scope or explicitly declined.
