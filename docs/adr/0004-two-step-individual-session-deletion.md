# 4. Two-Step Individual Session Deletion

Date: 2026-08-21

## Status

Accepted.

## Context

The session management dialog initially lacked itemized session controls and contained a bulk "DELETE ALL" action. Bulk deletion presented a severe operational risk of accidental data loss, wiping entire recorded sensor datasets with a single touch.

Captured sessions are valuable, non-reproducible sensor datasets consisting of high-resolution YUV frames, IMU readings, and exposure telemetry logs. A single misclick on a bulk delete button can permanently destroy hours of field capture work.

## Decision

Deprecate and remove the "DELETE ALL" bulk action entirely. Implement per-item individual session deletion with a mandatory two-step confirmation state (`[DEL]` -> `[CONFIRM?]`) within the session overlay UI.

## Consequences

- Accidental wiping of all recorded session data is completely eliminated.
- Users can selectively remove unwanted or corrupted session folders to free up storage space.
- The two-step touch confirmation prevents unintentional single-tap accidental deletions.
