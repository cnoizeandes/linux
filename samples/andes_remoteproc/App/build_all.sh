#!/bin/bash

#####################################
#             Build                 #
#####################################

echo $PATH
export | grep riscv
echo $LDFLAGS

make clean
make

tree ../Script
rm ../Script/echo_test ../Script/mat_mul_demo ../Script/proxy_app
tree ../Script
cp echo_test mat_mul_demo proxy_app ../Script
tree ../Script
