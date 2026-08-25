# ADR 0007: Bind local package trust and ownership to validated content

- Status: Accepted
- Date: 2026-08-09

## Context

Local packages need deterministic install/remove workflows without letting a manifest escape its
source/destination root, replace unrelated files, or retain executable trust after modification.

## Options considered

1. Copy local package trees and trust their package ID.
2. Trust a package path until the user revokes it.
3. Validate a bounded tree and bind ownership/trust to exact content.

## Decision

Package schema 2 uses typed entry points, engine compatibility and SPDX metadata. Installation
walks a bounded real directory, rejects traversal, collisions, links/reparse points and unsupported
entries, then writes an ownership marker and deterministic lock containing SHA-256 content
evidence. Executable entry points require `--allow-executable` and a separate trust record bound to
package identity, version and content hash. Removal only deletes content whose ownership marker
matches. Validation detects tampering before use.

## Rationale

An ID or path survives content replacement. A content digest does not, and an ownership marker
lets removal distinguish manager-owned files from unrelated user data.

## Positive consequences

- A modified executable package loses trust automatically.
- Install and lock output are deterministic and auditable.
- Removal fails closed on ownership or content mismatch.

## Negative consequences

The v1 product supports local directory packages, not remote registries, arbitrary archives,
signatures or automatic dynamic loading. Those require a separate distribution/security design.

## Reconsider when

Add archives, repositories, or signatures only with canonical signing input, key rotation,
revocation, downgrade protection, and equivalent traversal/ownership tests.
