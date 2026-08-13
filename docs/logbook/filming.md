# Filming logbook

How a run is filmed and why it is worth filming: three faults in the scene generator were found by looking at a picture and none by reading a number. A number without its conditions is not a result. Each entry names the apparatus, the method and the outcome. An entry that turned out to be invalid stays and says why: a run that is deleted teaches nothing twice.

## 2026-08-12: how a run is filmed, and why it is worth filming

Three bugs in the scene generator were found by looking at a picture and none by reading a
number, so rendering is part of the method rather than a way of publishing it. The numbers were
consistent, reproducible and wrong together each time: eighteen towers standing shoulder to
shoulder as one slab, a column launching itself nine metres upward, avatars spawned inside the
stack they were meant to knock over. A render showed each in seconds.

So the procedure is written down here beside the measurements it checks.

### The apparatus

- `determinism_probe --print-scene` emits the generated MJCF to stdout. Everything else depends
  on that: the scene is generated, so the only way to see what is being simulated is to ask it.
- The **Python `mujoco` package, pinned to the same 3.11.0 the repository vendors**. Same
  version, so what is rendered is what is simulated. It is a rendering dependency and is
  deliberately not in the build.
- `mujoco.Renderer` offscreen. MuJoCo's `simulate` GUI is disabled in `cmake/mujoco.cmake` and
  stays disabled — a viewer is a dependency the service does not need.
- `ffmpeg`, fed raw frames over a pipe.

### Two things that will waste an hour if forgotten

- The offscreen framebuffer defaults to 640×480 and is set **in the model**, not in the
  renderer: a `<visual><global offwidth= offheight=/></visual>` clause has to be injected before
  compiling, or `Renderer` refuses any larger size.
- The generated scene has **no lights and no colours**, because the simulation does not need
  them. Rendered as-is it is a black rectangle. Lights and `rgba` are injected into the copy
  that is filmed and never into the scene that is measured — a render that changed the model
  would be a picture of a different run.

### Frames go to the encoder, not to memory

1080×1920×3 is 6 MB a frame, so a twenty-three second clip is 4 GB of raw video. Buffering it in
a list and saving a `.npy` worked and then had to be read back; piping each frame to `ffmpeg`
as it is rendered costs nothing and bounds the memory.

### Realtime is a result, not a setting

Wall clock is measured while rendering and reported as a factor. An offline render played back
at 30 fps looks realtime whatever the simulation cost — a scene at 0.02× and a scene at 4× make
the same video. The flat field measured **4.15×** with 912 bodies and 3678 contacts; the same
run is honest to publish only because that number was taken.

### Citation travels in the file

`CITATION.cff` is read at encode time and its fields become container metadata — title, author,
ORCID, licence, version, date, and a comment naming the repository and pointing at the citation
file. A clip separated from the repository still says what it is and who made it, which a
filename does not.

Encoded as ProRes 422 HQ. It is a mezzanine format: large, meant for editing rather than
posting, and a smaller delivery encode is made from it rather than the reverse.

### What this is not

Not evidence of correctness. A render shows the shape of a scene and the shape of its failure;
it says nothing about whether the state replayed bit for bit, which is `determinism_probe`'s job
and is checked with `mj_getState` rather than with eyes.
