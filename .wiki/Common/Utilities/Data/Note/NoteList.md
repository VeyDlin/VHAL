# NoteList — Musical Note Frequencies

Musical note frequencies (Hz) from C0 (16.35 Hz) to B8 (7902.13 Hz). Useful for buzzer/speaker tone generation.

```cpp
#include <Utilities/Data/Note/NoteList.h>

float freq = NoteList::A4;   // 440.0 Hz (concert pitch)
float off  = NoteList::OFF;  // 0 Hz (silence)

// Play a melody
struct Note { float freq; uint32 durationMs; };
Note melody[] = {
    {NoteList::C4,  250},
    {NoteList::E4,  250},
    {NoteList::G4,  250},
    {NoteList::C5,  500},
    {NoteList::OFF, 100},
};
```

Note naming: `{Note}{Octave}`, sharps use `S` suffix (e.g. `CS4` = C#4, `FS5` = F#5).
