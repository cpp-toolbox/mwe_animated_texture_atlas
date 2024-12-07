# mwe_animated_texture_atlas

Minimal working example for animated texture atlases.

# details

When working with an animated texture atlas the atleas usually looks something like this: 

![image](https://github.com/user-attachments/assets/4524776d-8402-477b-900a-cff0c8940e53)

We then specify the animation order using the same syntax that can be found in the `texture_atlas` subproject, naming the individual frames "1", "2", ...  in the order you want them to appear.

This allows the user to look at the spritesheet and understand what the animation will look like and in what order it occurs in.

If you can, always generate your animation atlas in the standard reading order, then you can use the `json_spritesheet_generator` tool to automatically generate the required json file.
