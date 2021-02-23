# AdaptiveSampling
    Master Thesis in RTRT, applying spatial-temporal adaptive sampling in real-time. 
    
### Credit:
---

DXR12 Framework based on https://github.com/jpvanoosten/LearningDirectX12 but modified for compute
shaders and RTRT. 

    
### Meetings:
##### Meeting notes 23/2:
---
Add support for sampling several more than X spp. E.g 1000 spp. Need to remove timeout
in settings. 

Expect to work with 2-4 spp.

Don't focus too much on PBR - for the scope of the thesis - work with peter shirley's 
ray tracing in a weekend.

Try to work with a cornell box for reference, to compare with c solution. 

Also try to work with GPU solution, because CPU solution might be too different.

##### Meeting notes 16/2:
---
No notes, but <link> has PBR DXR implementation
    https://github.com/phgphg777/DXR-PathTracer/blob/master/DXRPathTracer/sampling.hlsli

##### Meeting notes 9/2:
---
To solve normal-maps mip-map approximation done using averaging, use lean mapping
instead. But regular mipmaps is good enough for now.
    https://www.csee.umbc.edu/~olano/papers/lean/
    
When calculating the hit position, of choosing to either use the interpolated value
or the generated ray and then calculate the hit position, both should be equivalent.

In terms of texture filtering, the texture filtering done by Ewins et al. is good enough. 

In PBR shading, the specular maps can be used to regulate the shininess, whilst trying
out default values for the other materials. 

##### Meeting notes 2/2:
---
Aim for some sort of model moving about in the scene, linear interpolation is enough.

Path sorting is NOT done automatically. Hard to implement in real time so don't.

Number of bounces is n, n = 5 is good enough for starters. It is normal to calc
shadow ray at each bounce, and discard ray chains which wont affect much. (< 5%).

More than 1 light, but less than 10 is good enough. Use some sort of heuristic to
select where to sample from, maybe closest or most important for scene? Point light
is good enough or directional light. Area light is potential to implement in future.

How detailed should the scenes be? Textures are of heavy cost, and should be implemented
but last. 

Some more scenes for testing can be seen in <LINK>. Mainly "Amazon Lumberyard Bistro",
"Sibenik Cathedral", and "Fireplace room". 
    https://casual-effects.com/g3d/data10/index.html#

    
##### Meeting notes 26/1:
---
Found another reference DXR system that displays a more complex model:
    https://github.com/acmarrs/IntroToDXR

##### Meeting notes 19/1:
---
Open source code is free to use. GO HAM.

###### Intro tutorials:
    http://rtintro.realtimerendering.com/
    http://intro-to-dxr.cwyman.org/

###### "Advance" tutorials/tips:
    https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/
    https://developer.nvidia.com/blog/profiling-dxr-shaders-with-timer-instrumentation/
    https://developer.nvidia.com/blog/rtx-best-practices/
    https://sites.google.com/view/arewedonewithraytracing

#### Resources:
    https://github.com/NVIDIAGameWorks/DxrTutorials
    
    



