// FileAssoc.h - per-user .pdf file association.
//
// Windows 10/11 do not allow applications to silently make themselves the
// default handler. We register Feather PDF as a *capable* PDF handler
// (it then appears in "Open with" and in Settings > Default apps) and then
// open the Default apps page so the user can confirm the choice.
// Everything is written under HKEY_CURRENT_USER: no admin rights needed.
#pragma once

bool RegisterFileAssociation();
void UnregisterFileAssociation();
void OpenDefaultAppsSettings();
