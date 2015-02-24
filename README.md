# VS-ContinuityFixer
Continuity Fixer port for Vapoursynth

# Usage
::
	edgefixer.ContinuityFixer(src, left, top, right, bottom, radius)

src, left, top, right, bottom are mandatory
radius is optional, it default to the shortest dimension of the clip [usually the height].


The plugin repair only the first plane [and will discart the others], for repairing all the planes do as follow
::
	#repair two left row on the luma plane and one left row on the chroma planes
	y = core.std.ShufflePlanes(src, [0], vs.Gray).edgefixer.ContinuityFixer(y,2,0,0,0)
	u = core.std.ShufflePlanes(src, [1], vs.Gray).edgefixer.ContinuityFixer(u,1,0,0,0)
	v = core.std.ShufflePlanes(src, [2], vs.Gray).edgefixer.ContinuityFixer(v,1,0,0,0)

	fix = core.std.ShufflePlanes([y,u,v], [0,1,2], vs.YUV)

# Known issues
For large repair value [the four sides] the plugin create strange artifact and is not pixel exact to the avs version, i don't know what cause this but for sane values [less then 10 pixel, maybe more] the output is the same as the avs version.

# Compilation
::
	g++ -c continuity.cpp -O2 -msse2 -mfpmath=sse -o continuity.o
	g++ -shared -Wl,--dll,--add-stdcall-alias -o continuity.dll continuity.o