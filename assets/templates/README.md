# Template assets

`Lato-Regular.ttf` is an unmodified Lato Regular font by Łukasz Dziedzic and
contributors, distributed under SIL OFL 1.1 in `OFL.txt`.

Source: https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Regular.ttf
SHA-256: d636e4683231f931eda222d588e944d082bfd3bdba02f928bee461c0f185b251
License source: https://raw.githubusercontent.com/google/fonts/main/ofl/lato/OFL.txt
License SHA-256: 74ba064d03f1f1c4a952da936c3eb71866c34404916734de3cae73b34357e59e

CMake copies these files and Chroma's Apache-2.0 license into the build and
installed template resource directory. `chroma init` copies the font and license
into new projects. The original six-second demo score is synthesized locally by
`src/cli/init.cpp`; no downloads, recordings, or external audio tools are used.

`demo-footage.mp4` is an original six-second 1280x720/30 abstract animation,
created entirely from Chroma shapes in `demo-footage.json`. It has no audio and
uses this repository's Apache-2.0 license. Rebuild it with the controlled release
renderer (OpenH264):

```sh
python3 assets/templates/generate-demo.py --chroma ./build-package/chroma
```

Checked-in clip SHA-256:
`a4d35efa7715ade44f352d08bb5af67371d8d2feba062084107216b955db4e92`.
The default template music is generated separately at project creation.
