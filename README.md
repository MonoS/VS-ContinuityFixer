# VS-ContinuityFixer
Continuity Fixer port for Vapoursynth, ![original code](https://github.com/sekrit-twc/EdgeFixer/tree/master/EdgeFixer)  

# Usage

	edgefixer.ContinuityFixer(src, left, top, right, bottom, radius)

src, left, top, right, bottom are mandatory.  
radius is optional, it default to the shortest dimension of the clip [usually the height].  


Since V6 the plugin can repair all the planes in a single call [for a maximum of three]

	#repair two left row on the luma plane and one left row on the chroma planes with a radius of 10 for the luma plane and 5 for the chroma planes
	fix = core.edgefixer.ContinuityFixer(src, [2,1,1], [0,0,0], [0,0,0], [0,0,0], [10,5,5])

# Known issues
<del>For large repair value [the four sides] the plugin create strange artifact and is not pixel exact to the avs version, i don't know what cause this but for sane values [less then 10 pixel, maybe more] the output is the same as the avs version.</del> Fixed in V5

# Compilation

	g++ -c continuity.cpp -O2 -msse2 -mfpmath=sse -o continuity.o
	g++ -shared -Wl,--dll,--add-stdcall-alias -o continuity.dll continuity.o

# Thanks
Mirkosp, JEEB, HolyWu, jackoneill and Myrsloik