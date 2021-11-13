### A script to blend image-based subtitles into a clip.


### Requirements - AviSynth+ 3.6+, masktools2, avsresize, PropToClip, SubImageFile.


### Usage ###
###
# BlendSubImage(clip c, string file, bool "info", string "matrix", string "transfer", string "primaries")
###
## Parameters ##
#---------------
# c: Input clip.
# The subtitles will be blended into it.
#---------------
# file: Subtitle file.
# For VOBSUB, it must the name of the idx file. The corresponding sub file must be in the same folder, and it must have the same name.
#---------------
# info (default false): Whether to display the information about each track includes the id, the language (if known), the resolution, and the format.
#---------------
# matrix/transfer/primaries (default "709"/"709"/"709"): They will be used when converting the RGB24 subtitles to YUV.


### Changelog ###
#---------------
# Initial version.


Function BlendSubImage(clip c, string file, bool "info", string "matrix", string "transfer", string "primaries")
{
    Assert(!IsRGB(c), "BlendSubImage: clip must be in YUV format.")
    
    info_ = Default(info, false)
    matrix = Default(matrix, "709")
    transfer = Default(transfer, "709")
    primaries = Default(primaries, "709")

    subs = SubImageFile(c, file, info=(info_) ? true : false)
    alpha = PropToClip(subs)
    Layer(BlankClip(alpha), z_ConvertFormat(subs, pixel_type="y8", colorspace_op="rgb:"+transfer+":"+primaries+"=>"+matrix+":"+transfer+":"+primaries))
    mt_merge(BlankClip(last), last, alpha)    
    premultiplied = z_ConvertFormat(last, Width(c), Height(c), pixel_type=PixelType(c), resample_filter="bicubic")
    alpha = ConvertBits(alpha, BitsPerComponent(c))
    mt_merge(c, premultiplied, alpha, luma=true)
    
    if (info_)
    {
        current_frame = 0
        Text(propGetString(subs, "text"), text_color=color_khaki)
    }
}
