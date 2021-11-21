## Description

Renders image-based subtitles such as VOBSUB and PGS.

It returns a list of two clips. The first one is an RGB24 clip containing the rendered subtitles. The second one is a Gray8 clip containing a mask, to be used for blending the rendered subtitles into other clips.

BlendSubtitles.avsi can be used for automatic blending.

This is a port of the VapourSynth sub.ImageFile.

### Requirements:

- AviSynth+ 3.7.x r3446 or later

- Microsoft VisualC++ Redistributable Package 2022 (can be downloaded from [here](https://github.com/abbodi1406/vcredist/releases))

### Usage:

```
SubImageFile (clip input, string file, int "id", int[] "palette", bool "gray", bool "info", bool "flatten")
```

### Parameters:

- input\
    The frame rate and number of frames will be obtained from this clip.
        
- file\
    Name of the subtitle file.\
    For VOBSUB, it must the name of the idx file. The corresponding sub file must be in the same folder, and it must have the same name.
    
- id\
    Id of the subtitle track to render.\
    There may be several subtitle tracks in the same file.\
    If this is -1, the first supported subtitle track will be rendered.\
    Default: -1.
    
- palette\
    Custom palette.\
    This is an array of at most 256 integers. Each element's least significant four bytes must contain the values for alpha, red, green, and blue, in that order, from most significant to least.\
    Additionally, the special value 2**42 means that the corresponding element of the original palette is used. This way it is possible to override only the third element, without overriding the first and second ones, for example.\
    An alpha value of 255 means the colour will be completely opaque, and a value of 0 means the colour will be completely transparent.
    
- gray\
    If True, the subtitles will be turned gray.\
    Default: False.
    
- info\
    If this is True, a list of all supported subtitle tracks found in the file will be saved as frame property "text".\
    The information about each track includes the id, the language (if known), the resolution, and the format.\
    Default: False.
    
- flatten\
    If this is True, ImageFile will output a clip with exactly as many frames as there are pictures in the subtitle file.\
    Default: False. 

### Building:

- ffmpeg is required.

- Windows\
    Use solution files.

- Linux
    ```
    Requirements:
        - Git
        - C++14 compiler
        - CMake >= 3.16
    ```
    ```
    git clone https://github.com/Asd-g/AviSynthPlus-SubImageFile && \
    cd AviSynthPlus-SubImageFile && \
    mkdir build && \
    cd build && \
    
    cmake ..
    make -j$(nproc)
    sudo make install
    ```
