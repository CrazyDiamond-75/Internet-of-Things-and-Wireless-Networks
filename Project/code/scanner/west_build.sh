#!/bin/bash

# ASSUMES WEST AND ZEPHYR INSTALLED IN ~/zephyrproject

CWD=$(pwd)

cd ~/zephyrproject
source .venv/bin/activate

west build --pristine=always -b esp32_devkitc/esp32/procpu "$CWD" --build-dir "$CWD/build"
