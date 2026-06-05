#!/bin/bash

# ASSUMES WEST AND ZEPHYR INSTALLED IN ~/zephyrproject

CWD=$(pwd)

cd ~/zephyrproject
source .venv/bin/activate

west flash --build-dir "$CWD/build"
