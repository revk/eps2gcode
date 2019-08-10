eps2gcode: eps2gcode.c
	cc -O -o  $@ $< -lpopt -lm
