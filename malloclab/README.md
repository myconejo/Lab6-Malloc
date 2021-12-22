# LAB 6: Malloc Lab

## 1. Main Files

```bash
mm.{c,h} - Your solution malloc package.

mdriver.c - The malloc driver that tests your mm.c file

short{1,2}-bal.rep - Two tiny tracefiles to help you get started. 

Makefile - Builds the driver
```

## 2. Other support files for the driver

- `config.h` Configures the malloc lab driver

- `fsecs.{c,h}` Wrapper function for the different timer packages

- `clock.{c,h}` Routines for accessing the Pentium and Alpha cycle counters

- `fcyc.{c,h}` Timer functions based on cycle counters

- `ftimer.{c,h}` Timer functions based on interval timers and gettimeofday()

- `memlib.{c,h}` Models the heap and sbrk function


## 3. Testing Malloc with Mdriver

To build the driver, type "make" to the shell.

To run the driver on a tiny test trace:

```bash
unix> mdriver -V -f short1-bal.rep
```

The -V option prints out helpful tracing and summary information.


To get a list of the driver flags:

```bash
unix> mdriver -h
```
