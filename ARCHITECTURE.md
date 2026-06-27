# Navigation Flow Map

```
App Start
    |
    v
screen-main  (dashboard)
    |-- [Mixes button]          --> screen-mixes
    |-- [Tapes button]          --> screen-tapes
    |-- [New MixTape button]    --> screen-mix-tapes
    |-- [Config button]         --> screen-config
    |-- [Exit button]           --> screen-confirm-exit
    |-- [Esc key]               --> screen-confirm-exit


screen-mixes
    |-- [Add New Mix button]    --> selectMixPaths (native dialog)
    |                               --> screen-add-mix-details
    |-- [Edit button] (per mix) --> screen-add-mix-details
    |-- [Delete button]         --> screen-delete-blocked  (if mix is in use)
    |                               or confirm() then stays on screen-mixes
    |-- [Go Back / Esc]         --> screen-main


screen-add-mix-details
    |-- [+ Add Files button]    --> selectMixPaths (native multi-file dialog)
    |                               appends selected paths, stays on screen
    |-- [+ Add Folders button]  --> selectMixFolders (native folder dialog)
    |                               appends selected folder, stays on screen
    |-- [Remove button]         --> removes path from list, stays on screen
    |-- [Save Mix button]       --> saves, --> screen-mixes
    |-- [Cancel / Esc]          --> screen-mixes


screen-tapes
    |-- [Add New Tape button]   --> selectTapeFolder (native folder dialog)
    |                               --> screen-add-tape-details
    |-- [Edit button] (per tape)--> screen-add-tape-details
    |-- [Delete button]         --> screen-delete-blocked  (if tape is in use)
    |                               or confirm() then stays on screen-tapes
    |-- [Go Back / Esc]         --> screen-main


screen-add-tape-details
    |-- [Change Folder button]  --> selectTapeFolder (native folder dialog)
    |                               updates path, stays on screen
    |-- [Save Tape button]      --> writes .mixtape marker, saves, --> screen-tapes
    |-- [Cancel / Esc]          --> screen-tapes


screen-mix-tapes  (create / edit a Mix-Tape)
    |-- [Save Mix-Tape button]  --> saves, --> screen-main
    |-- [Cancel / Esc]          --> screen-main


screen-main  (Existing Mix-Tapes list, inline)
    |-- [Apply button]          --> rsync (background thread pool)
    |                               button shows "Running..." while active
    |-- [Edit button]           --> screen-mix-tapes  (pre-populated)
    |-- [Delete button]         --> confirm() then stays on screen-main


screen-config
    |-- [Go Back / Esc]         --> screen-main


screen-confirm-exit
    |-- [Yes, Exit button]      --> exitApp (terminates process)
    |-- [No, Stay / Esc]        --> screen-main


screen-delete-blocked
    |-- [Go Back button]        --> screen-main
    |-- [Go to Main Screen]     --> screen-main
    |-- [Esc]                   --> screen-main
```
