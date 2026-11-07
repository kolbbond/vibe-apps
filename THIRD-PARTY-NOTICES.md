# Third-party notices

The vibe-apps **source code** is MIT licensed (see [LICENSE](LICENSE)).

The **prebuilt packages** (the Linux AppImage and tarball, and the Windows installer and
zip) also contain the third-party libraries below. Some of them are under the GNU GPL:
FFTW, and on Linux FFmpeg as built by the distribution (with x264, x265 and other GPL
codecs). **The packages as a whole are therefore distributed under the GNU General
Public License, version 2 or later** ([licenses/GPL-2.0-or-later.txt](licenses/GPL-2.0-or-later.txt)).
The complete source is available at <https://github.com/kolbbond/vibe-apps> and
<https://github.com/kolbbond/vibe>. The release notes name the engine tag that each
package was built from.

## Built from source with the vibe engine

| Component | Copyright | License |
|---|---|---|
| [vibe](https://github.com/kolbbond/vibe) engine | kolbbond | MIT |
| [simple-magnum-gui](https://github.com/kolbbond/simple-magnum-gui) | kolbbond | MIT |
| [Magnum](https://magnum.graphics), Corrade, magnum-plugins, magnum-integration | Vladimír Vondruš and contributors | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut | MIT |
| [ImPlot](https://github.com/epezent/implot) | Evan Pezent | MIT |
| [ImPlot3D](https://github.com/brenocq/implot3d) | Breno Cunha Queiroz | MIT |
| [ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) | Stephane Cuillerdier | MIT |
| [magnum-bloom](https://gitlab.com/jeroen.van.nugteren/magnum-bloom) | Jeroen van Nugteren | Unlicense (public domain) |
| [libsoundio](https://github.com/andrewrk/libsoundio) | Andrew Kelley | MIT |
| [AudioFile](https://github.com/adamstark/AudioFile) | Adam Stark | MIT |
| [jsoncpp](https://github.com/open-source-parsers/jsoncpp) | Baptiste Lepilleur and The JsonCpp Authors | MIT |
| [Armadillo](https://arma.sourceforge.net) | Conrad Sanderson and contributors | Apache 2.0 ([licenses/Apache-2.0.txt](licenses/Apache-2.0.txt)) |
| [SuperLU](https://github.com/xiaoyeli/superlu) | The Regents of the University of California | BSD 3-clause |

The MIT components are used under these terms:

> Permission is hereby granted, free of charge, to any person obtaining a copy of this
> software and associated documentation files (the "Software"), to deal in the Software
> without restriction, including without limitation the rights to use, copy, modify,
> merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
> permit persons to whom the Software is furnished to do so, subject to the following
> conditions: The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
> WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

### Fonts (compiled into simple-magnum-gui)

| Font | Copyright | License |
|---|---|---|
| Cousine, JetBrains Mono (Nerd Font), Karla, Source Sans Pro | Google; The JetBrains Mono Project Authors; Jonathan Pinhorn; Adobe | SIL OFL 1.1 ([licenses/OFL-1.1.txt](licenses/OFL-1.1.txt)) |
| Droid Sans, Roboto | Google | Apache 2.0 ([licenses/Apache-2.0.txt](licenses/Apache-2.0.txt)) |
| ProggyClean, ProggyTiny | Tristan Grimmer | MIT |

## System libraries bundled in the packages

| Library | License |
|---|---|
| [FFTW 3](https://www.fftw.org) (Matteo Frigo and Massachusetts Institute of Technology) | GPL 2.0 or later |
| [FFmpeg](https://ffmpeg.org) (libavformat, libavcodec, libswresample, libavutil) and its codec libraries | LGPL 2.1 or later; GPL 2.0 or later as built by Linux distributions |
| [SDL2](https://libsdl.org) (Sam Lantinga) | zlib |
| [Boost](https://www.boost.org) | Boost Software License 1.0 ([licenses/BSL-1.0.txt](licenses/BSL-1.0.txt)) |
| OpenBLAS, LAPACK | BSD 3-clause |
| PulseAudio, ALSA, JACK client libraries (Linux) | LGPL 2.1 or later |

The **AppImage** bundles more libraries pulled in by the ones above. Their Ubuntu
copyright files are included in the AppImage under `usr/share/doc/<package>/copyright`.
