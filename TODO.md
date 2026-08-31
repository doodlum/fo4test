
## Flashlight vs static glass (user-reported 2026-08-30, previs-independent)
The Pip-Boy light does not affect the chem-lab glass until the player moves
away; toggling tpc does not change this, so it is not previs-gated.  Mechanism
hypothesis from the previs work: render-pass lights come from
BSFadeNode::lightData, attached one-shot at fade-in (REL::ID(2316460) guard:
lights-attached latch bit 51 clear && (bit 33 || root+0x228 dirty || mid-fade));
walking away triggers the mid-fade re-attach, which is why distance "fixes" it.
If the dynamic-light add path fails to set the root's lights-dirty byte
(+0x228), stationary nodes never refresh.  Candidate fix: set root[idx]+0x228
(roots at REL::ID(2712479), index byte REL::ID(2712516)) when dynamic lights
spawn/move, or investigate ShadowSceneNode::ProcessQueuedLights' event path.
Addendum: walking BACK toward the glass makes the flashlight effect disappear
again -- the fade-transition re-attach rebuilds the list and the near-state
rebuild excludes the dynamic light.  Deferred until fix v2 is done.

## v2 root-cause dossier (2026-08-30, end of session state)
PROVEN: previs-mode glass renders via the precombined/packed pipeline; `sco`
alone (combined off, previs culling on) produces a frame identical to tpc-off
(5,015 px = cross-session noise) -- the bug lives in packed-mesh lighting, not
previs culling.  FALSIFIED as fixes (each with paired cross-run methodology):
per-node light attach (2317475), per-node engine update (2316460, bit-33
forced), pass-cache invalidation (lightListChanged bump), packed-null-fadeNode
proxy (premise was a capture artifact; all visible props have fadeNodes), and
consumer-gate composites (sites 9/15/16 + record branch).  MEASURED: per-node
lightData lists are EMPTY in both modes (fence 0xFFFFFFFF, count 0) -- point
lights are not the glass mechanism; the visual defect is the environment /
ambient source.  GetRenderPasses hook shows near-glass properties are only
asked for SHADOW passes (mode 24), never forward -- forward rendering of the
scene goes through the packed/instanced path (BSMTAManager), which builds its
passes internally.  NEXT CAMPAIGN: reverse the BSMTAManager instanced
accumulation (how packed passes obtain lighting/env constants) and bind the
room/ambient state there; alternatively, per-shape transparent exclusion at
packed-instance build time (render originals for transparent members only).
The v1 gate-detour fix works precisely because it routes drawing to the
originals in interiors.
