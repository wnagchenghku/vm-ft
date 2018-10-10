cd ~
cd vm-ft/qemu
git reset 6db228f3775c6d230b8c30b84e8b4c0b29ff8857 --hard
git pull origin boxuan/failover
sudo make -j`nproc`
cd ~
