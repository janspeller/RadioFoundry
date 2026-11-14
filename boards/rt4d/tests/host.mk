# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# This board's host tests, included by tests/host/Makefile and run from there. HERE is this directory, read while the
# file is included; recipes use $< (the test, first) and $^ instead, which keep the paths they were given
HERE := $(dir $(lastword $(MAKEFILE_LIST)))

# The factory calibration records (hardware-rt4d.md 3.2)
TESTS   += rt4dCalTest
rt4dCalTest: $(HERE)rt4dCalTest.c $(HERE)../cal_rt4d.c $(HERE)../cal_rt4d.h
	$(CC) $(CFLAGS) -I$(ROOT)/hal -I$(dir $<).. -Wshadow -o $@ $(filter %.c,$^)

run-boards:: rt4dCalTest
	./rt4dCalTest
