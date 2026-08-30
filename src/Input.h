#pragma once

// Synthetic input for driving the main menu.
//
// The main menu's Continue path runs through Scaleform: the button handler
// sets queueContinueGame and invokes a movie function, and the actual load
// only happens on the Flash callback.  Poking the flag and calling the invoke
// from a background task does not reproduce that -- the movie ignores it until
// the menu has been engaged by real input.  Pressing the key is what a person
// does, and it exercises the same path the game ships.
namespace Input
{
	// Brings the game window to the foreground.  SendInput goes to whatever
	// has focus, so this has to succeed first.
	bool FocusGameWindow();

	// Presses and releases a virtual key (VK_RETURN, VK_SPACE, 'E', ...).
	void TapKey(std::uint16_t a_vk);
}
