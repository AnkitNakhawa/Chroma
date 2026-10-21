# Start a Chroma project

`chroma init` is available on the development `main` branch and in its package
artifacts. **The published v0.1.0 archives predate this command.** Build current
main or use a newer package containing it. Full rendering needs media and text
support; the portable packages include both.

## Create and render

With the current CLI on your PATH:

```sh
chroma init my-video
cd my-video
chroma preflight scene.json
chroma render scene.json --output video.mp4 --progress text
```

The default is `title-sequence`. Each template produces a six-second 1280×720,
30 fps composition, a licensed Lato font, and a quiet original music cue. No
network, Python, stock downloads, or external media tools are needed at creation
time. Captioned footage includes an original abstract demo clip.

The destination must not exist, and its parent directory must exist. Existing
files, empty directories, and symlinks are refused; there is no `--force` option.

## Three templates

```sh
chroma init --list-templates
chroma init opening --template title-sequence --title "A fresh perspective." --subtitle "A film by your team"
chroma init launch --template product-promo --title "Meet your next favorite." --accent '#FF9A66'
chroma init story --template captioned-footage --title "A day worth remembering."
```

| Template | Layout | Main edits in `scene.json` |
| --- | --- | --- |
| `title-sequence` | Centered animated title, eyebrow, subtitle, accent, and moving background shape | `title`, `subtitle`, `eyebrow`, and `footer` text layers |
| `product-promo` | Two-column product presentation, native device mockup or supplied image, and call to action | `title`, `subtitle`, `cta`, image asset, and hero-card shapes |
| `captioned-footage` | Full-frame video, header, and three animated caption cards | `title`, `caption-1`, `caption-2`, `caption-3`, and the footage asset |

Captions are manually authored, with initial ranges `[0,60)`, `[60,120)`, and
`[120,180)`. These ranges are frame-based and end-exclusive. `--subtitle` sets the
first caption; edit the remaining cues in JSON. This command does not transcribe
speech or import a video's audio automatically.

## Supply your own content

```sh
chroma init launch --template product-promo --image ./product.png --title "Made for everyday adventures."
chroma init story --template captioned-footage --footage ./clip.mp4 --subtitle "Your opening caption."
chroma init opening --font ./brand-font.ttf --music ./score.wav --accent '#7ADCCB'
chroma init silent-opening --no-music
```

| Option | Behavior |
| --- | --- |
| `--template NAME` | Select one of the three names above; defaults to `title-sequence` |
| `--title TEXT` | Replace the headline; wrapping and ellipsis keep it inside its layout box |
| `--subtitle TEXT` | Replace the subtitle, or the first caption in `captioned-footage` |
| `--accent '#RRGGBB'` | Set the main accent color; quote the `#` in your shell |
| `--font FILE` | Copy a supplied font instead of the bundled Lato font |
| `--music FILE` | Copy a supplied audio asset instead of synthesizing the demo score |
| `--no-music` | Omit the score and audio track; cannot be combined with `--music` |
| `--image FILE` | Copy a product image; only valid for `product-promo` |
| `--footage FILE` | Copy a video clip; only valid for `captioned-footage` |

Supplied media and font files are copied into the project, leaving the originals
unchanged. Paths inside `scene.json` are relative, so move or share the entire
project directory. Input paths on the command line are relative to your current
working directory. Music and footage need at least six seconds for the default
timeline. For shorter media, shorten the canvas and relevant layer/track ranges
and adjust their animation keyframes.

`init` checks that supplied files are readable, regular, and nonempty. It does
not transcode, probe their full compatibility, or trim them. Run
`chroma preflight scene.json` after creation to check fonts, stream types,
decoders, and timing. Existing [preflight limitations](preflight.md) still apply.

## What gets created

```text
my-video/
  scene.json                  # Standard schema-v1 Chroma composition
  README.md                   # Render commands and editing notes
  .gitignore                  # Default preview/render outputs
  assets/
    font.ttf                  # Copied font
    FONT-LICENSE.txt          # License for the default Lato font
    CHROMA-LICENSE.txt         # License for original Chroma demo media
    music.wav                 # Default score, unless omitted or replaced
    footage.mp4               # Captioned-footage default; custom extension retained
```

A supplied product image is stored as `assets/product.<extension>`. Supplied
music and footage retain their extensions; they are not converted by `init`.
The bundled font keeps its SIL OFL 1.1 license. Original demo music and footage
use Apache-2.0; supplied content retains its own licensing.

There is no separate project format or regeneration step. Creation options fill
in the initial JSON once. Continue by editing `scene.json`; re-running `init`
will not update or overwrite the project. The CLI, C API, and Python wrapper all
load this same file.

To inspect a still frame before rendering:

```sh
chroma frame scene.json --frame 75 --output preview.ppm
```

For full schema details, see the [composition format](composition-format.md),
[text layout](composition-format.md#text-layout), and
[progress and cancellation](render-control.md).
