# pull latest code
cd ~/vm-ft
git reset 6db228f3775c6d230b8c30b84e8b4c0b29ff8857 --hard
git pull origin boxuan/failover

# recompile rdma-paxos
cd ~/vm-ft/rdma-paxos/target
make

# recompile qemu
cd ~/vm-ft/qemu
sudo make -j`nproc`
cd ~
