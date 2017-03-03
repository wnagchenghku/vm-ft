## How to setup/test COLO

Tested using Ubuntu 16.04.2, MLNX_OFED_LINUX-3.4-2.0.0.0-ubuntu16.04-x86_64

### Test environment prepare
RDMA Paxos
```
# export VMFT_ROOT=<absolute path of vm-ft>
# cd rdma-paxos/target
# apt-get install libconfig-dev libdb5.3-dev libev-dev 
# make
```
Qemu colo
```
# cd qemu
# apt-get install libglib2.0-dev zlib1g-dev libpixman-1-dev libgcrypt11-dev libcurl4-gnutls-dev libgnutls-dev libaio-dev libfdt-dev libtool
# ./configure --disable-werror --target-list=x86_64-softmmu --extra-ldflags="-Wl,--no-as-needed -L$VMFT_ROOT/rdma-paxos/target -linterpose -lnl-3 -lnl-cli-3 -lnl-route-3 -lnl-3 -lnl-cli-3 -lnl-route-3" --extra-cflags="-I$VMFT_ROOT/rdma-paxos/src/include/rsm-interface -I/usr/include/libnl3 -I/usr/include/libnl3"
```

### Test steps
**Note: Here the primary side host ip is 10.22.1.2, secondary side host ip is 10.22.1.3.**

(1) Startup qemu:

*Primary side:*
```
x86_64-softmmu/qemu-system-x86_64 -enable-kvm -boot c -m 2048 -smp 2 -qmp stdio -vnc :7 -name primary \
-cpu qemu64,+kvmclock -device piix3-usb-uhci -drive if=virtio,id=colo-disk0,driver=quorum,read-pattern=fifo,vote-threshold=1,children.0.file.filename=/mnt/sdd/pure_IMG/linux/redhat/rhel_6.5_64_2U_ide,children.0.driver=raw \
-S -netdev tap,id=hn0,vhost=off,script=/etc/qemu-ifup,downscript=/etc/qemu-ifdown \
-device e1000,id=e0,netdev=hn0,mac=52:a4:00:12:78:66
```
*Secondary side:*
```
# qemu-img create -f qcow2 /mnt/ramfs/active_disk.img 10G

# qemu-img create -f qcow2 /mnt/ramfs/hidden_disk.img 10G

x86_64-softmmu/qemu-system-x86_64 -boot c -m 2048 -smp 2 -qmp stdio -vnc :7 -name secondary -enable-kvm -cpu qemu64,+kvmclock \
-device piix3-usb-uhci -drive if=none,id=colo-disk0,file.filename=/mnt/sdd/pure_IMG/linux/redhat/rhel_6.5_64_2U_ide,driver=raw,node-name=node0 \
-drive if=virtio,id=active-disk0,driver=replication,mode=secondary,file.driver=qcow2,top-id=active-disk0,file.file.filename=/mnt/ramfs/active_disk.img,file.backing.driver=qcow2,file.backing.file.filename=/mnt/ramfs/hidden_disk.img,file.backing.backing=colo-disk0 \
-netdev tap,id=hn0,vhost=off,script=/etc/qemu-ifup,downscript=/etc/qemu-ifdown -device e1000,netdev=hn0,mac=52:a4:00:12:78:66 \
-chardev socket,id=red0,path=/dev/shm/mirror.sock -chardev socket,id=red1,path=/dev/shm/redirector.sock \
-object filter-redirector,id=f1,netdev=hn0,queue=tx,indev=red0 -object filter-redirector,id=f2,netdev=hn0,queue=rx,outdev=red1 -incoming tcp:10.22.1.3:8888 
```
(2) libnl:
```
# modprobe ifb numifbs=100 # (or some large number)
# ip link set up ifb0 # <= corresponds to tap device 'tap0'
# tc qdisc add dev tap0 ingress
# tc filter add dev tap0 parent ffff: proto ip pref 10 u32 match u32 0 0 action mirred egress redirect dev ifb0
```
(3) On Secondary VM's QEMU monitor, issue command
```
{'execute':'qmp_capabilities'}
{ 'execute': 'nbd-server-start',
  'arguments': {'addr': {'type': 'inet', 'data': {'host': '10.22.1.3', 'port': '8889'} } }
}
{'execute': 'nbd-server-add', 'arguments': {'device': 'colo-disk0', 'writable': true } }
```
***Note:***

a. Active disk, hidden disk and nbd target's length should be the same.

(4) On Primary VM's QEMU monitor, issue command:
```
{'execute':'qmp_capabilities'}
{ 'execute': 'human-monitor-command',
  'arguments': {'command-line': 'drive_add -n buddy driver=replication,mode=primary,file.driver=nbd,file.host=10.22.1.3,file.port=8889,file.export=colo-disk0,node-name=node0'}}
{ 'execute':'x-blockdev-change', 'arguments':{'parent': 'colo-disk0', 'node': 'node0' } }
{ 'execute': 'migrate-set-capabilities',
      'arguments': {'capabilities': [ {'capability': 'x-colo', 'state': true } ] } }
{ 'execute': 'migrate', 'arguments': {'uri': 'tcp:10.22.1.3:8888' } }
```