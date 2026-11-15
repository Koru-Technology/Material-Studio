This repository contains everything needed to build Material Studio except the C compiler itself.

## `/base`

Contains a general-purpose 3D engine.

#### `/sources`

A collection of C source files for the complete 3D engine, hardware abstraction, the macOS / iOS and Metal specifics (`/backends`) and some helper libraries (`/libs`). Only a minimal single file / header libraries are used.

#### `/tests`

Small stand-alone projects for testing.

#### `/tools`

- `/amake`: the build tool
- `/bin`: prebuilt amake for macOS
- `/iris`: sources of the neural tool used by the neural nodes

#### `/project.c`

This file adds C source files, include directories, libraries and defines to the project, depending on the selected platform (macOS or iOS) and build flags.

## `/paint`

Contains Material Studio specific code & assets only. The `project.c` file adds this specific code to the final project.
