# Parity Game Solver

A C++ program that can determine whether a parity game is winnable for the even player when given an extended HOA file as input. This project uses Zielonka's algorithm to solve the games. 

## Usage
### Compile
This project uses cmake. To build an executable use:
```
    mkdir ParityGameSolver/build
    cd ParityGameSolver/build
    cmake ..
    make
```

This creates the executable in the subdirectory ``build`` in ``ParityGameSolver``.  
**Note**: The project uses [OxiDD](https://oxidd.net) to manage BDDs. For this reason, Rust needs to be installed before building the executable.  

### Execution
To analyse a HOA, use:
```
    ./pgsolver <path> (<timeout>)
```
The parameters are:  
- **path**: (relative) filepath to the .ehoa file to use, example: ``../inputs/myParityGame.ehoa``. Ensure the provided file exists and is valid, otherwise an error will be produced and the execution will stop.
- **timeout**: optional parameter for the timeout in seconds, example: ``120``. The program will attempt to solve the parity game and abort if there is no result after the specified timeout. If no timeout is provided, a default of *150 seconds* will be used.

### Output
The output will consist of the path to the used .ehoa file and the result of the analysis (or timeout).  
Example:  
```
../../inputs/tests/Automata.ehoa
Realizable: 1
```
In this example, a winning strategy for the even player exists. In the opposite case, the output will be 0 instead. 

## Repository Structure
The project's main code is located in the file ``ParityGameSolver.cpp``. It contains the code to convert omega automata to parity games with BDD tranition labels and Zielonka's algorithm with attractor computation.

The files ``hoalexer.*``, ``hoaparser.*`` and ``simplehoa.*`` are helpers to parse the input files. They were adopted and partially adapted from the [hoa-tools](https://github.com/SYNTCOMP/hoa-tools/) repository.