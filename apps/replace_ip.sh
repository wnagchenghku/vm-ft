#!/bin/bash
IP=10.22.1.14
H_IP=127.0.0.1
mks=`ls */mk`
for mk in $mks;do
	sed -i "s/$IP/$1/g" $mk
done
