/* Core Forth definitions

To make this implementation of Forth as portable as possible, we need to
implement as much of it in Forth. However, forst definitions are less efficient
than natives one, so you'll also want to have a native version of those
definitions to go alongside.

This unit is designed to be easily swappable.
*/

void init_core_defs();

