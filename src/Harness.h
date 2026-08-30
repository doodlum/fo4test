#pragma once

// Drives the previs repro end to end with no operator input:
//
//   main menu -> coc <cell> -> wait for load -> setup commands
//     -> capture 01_previs_on.bmp
//     -> tpc
//     -> capture 02_previs_off.bmp
//     -> result.json -> qqq
//
// The sequencing runs on a worker thread and marshals every game-touching
// step onto the main thread through F4SE's task interface.
namespace Harness
{
	// Called from the kGameDataReady message, i.e. once the main menu exists.
	// Safe to call more than once; only the first call starts the run.
	void Start();
}
