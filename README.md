# Parity Game Solver

A C++ program that can determine whether a parity game is winnable when given an extended HOA file as input.

## Usage
This project uses cmake. To build an executable use:
```
    mkdir ParityGameSolver/build
    cd ParityGameSolver/build
    cmake ..
    make
```

This creates the executable in the subdirectory build in ParityGameSolver.  
**Note**: The project uses OxiDD to manage BDDs. For this reason, Rust needs to be installed before building the executable.  

To analyse a HOA, use:
```
    ./pgsolver path/to/your.ehoa
```