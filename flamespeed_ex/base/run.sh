#!/bin/bash

# Please use this file like bellow
# $ sh run.sh 1.1 1.0

phi=$1
eField=$2

fname=phi${phi}_eField${eField}

mkdir $fname

g++ flamespeed.cpp -o flamespeed -pthread -O0 -std=c++0x -lcantera
./flamespeed $phi $eField > ${fname}/log_${fname}

mv *.csv $fname
mv *.xml $fname