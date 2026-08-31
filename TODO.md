
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
