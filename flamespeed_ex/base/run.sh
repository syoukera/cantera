#!/bin/bash

phi=0.9
eField=1.0

g++ flamespeed.cpp -o flamespeed -pthread -O0 -std=c++0x -lcantera
./flamespeed $phi $eField