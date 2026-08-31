
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

## v2 session-end addendum
GetRenderPasses mode map: 0x15/0 = lit forward path (per-pass lights from
fadeNode lightData via 0x142240540, up to 16); 0x18/0x12 = deferred/unlit
passes (FUN_14221d4a0(pass, 0, 0) unconditionally).  Near-glass properties are
only ever asked for mode-24 (0x18) passes IN BOTH PREVIS STATES, so the
lighting divergence is not per-pass lights at all -- it lives in the deferred
(clustered) light assignment for packed geometry.  Next tool: RenderDoc-grade
GPU capture of ON vs OFF frames, or RE of the deferred light-cluster build for
packed instances.  Modes 9-14 in PrevisLightingFix are the falsified
hypotheses ladder, kept for the record.

## v2 final localization (CPU-side search complete)
Render passes for all near geometry are BYTE-IDENTICAL across the tpc toggle
(GRP raw-byte diff, 12 common geometries incl. the FX glass pass).  Every
CPU-visible render input is invariant: batch content*, properties, fade nodes,
per-node light lists, pass chains.  (*with previs on the batch differs, but
the passes built from it do not.)  The lighting divergence enters at pass
execution via per-frame GPU shader constants -- directional ambient /
clustered light data bound in BSLightingShader::SetupGeometry and the
accumulator's per-frame state.  Continue with: (a) RenderDoc capture of ON vs
OFF frames diffing bound constant buffers for one glass draw call, then (b)
Ghidra on the constant-buffer fill path that previs state feeds.
