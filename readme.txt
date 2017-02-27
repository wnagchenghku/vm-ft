
Tested using Ubuntu 16.04.2, MLNX_OFED_LINUX-3.4-2.0.0.0-ubuntu16.04-x86_64

export VMFT_ROOT=<absolute path of vm-ft>

cd $VMFT_ROOT/rdma-paxos/target
apt-get install libconfig-dev libdb5.3-dev libev-dev 
make clean
make

cd $VMFT_ROOT/qemu
apt-get install libglib2.0-dev zlib1g-dev libpixman-1-dev libgcrypt11-dev libcurl4-gnutls-dev libgnutls-dev libaio-dev libfdt-dev libtool

./configure --disable-werror --target-list=x86_64-softmmu --extra-ldflags="-Wl,--no-as-needed -L$VMFT_ROOT/rdma-paxos/target -linterpose -lnl-3 -lnl-cli-3 -lnl-route-3 -lnl-3 -lnl-cli-3 -lnl-route-3" --extra-cflags="-I$VMFT_ROOT/rdma-paxos/src/include/rsm-interface -I/usr/include/libnl3 -I/usr/include/libnl3"

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$VMFT_ROOT/rdma-paxos/target

mkdir -p /local/ubuntu
mount 202.45.128.160:/ubuntu /local/ubuntu

Here the leader side host ip is 10.22.1.2, major backup side host ip is 10.22.1.3.

Leader side:
sudo server_type=start server_idx=0 group_size=3 config_path=$VMFT_ROOT/rdma-paxos/target/nodes.local.cfg x86_64-softmmu/qemu-system-x86_64 -enable-kvm -boot c -m 2048 -smp 2 -qmp stdio -vnc :7 -name primary -cpu qemu64,+kvmclock -device piix3-usb-uhci -drive if=virtio,id=colo-disk0,driver=quorum,read-pattern=fifo,vote-threshold=1,children.0.file.filename=/ubuntu-vm/ubuntu-cheng.img,children.0.driver=raw -S -netdev tap,id=hn0,vhost=off,script=/etc/qemu-ifup,downscript=/etc/qemu-ifdown -device e1000,id=e0,netdev=hn0,mac=52:a4:00:12:78:66

$ modprobe ifb numifbs=100 # (or some large number)

$ ip link set up ifb0 # <= corresponds to tap device 'tap0'
$ tc qdisc add dev tap0 ingress
$ tc filter add dev tap0 parent ffff: proto ip pref 10 u32 match u32 0 0 action mirred egress redirect dev ifb0

Major backup side:
server_type=start server_idx=1 group_size=3 config_path=$VMFT_ROOT/rdma-paxos/target/nodes.local.cfg x86_64-softmmu/qemu-system-x86_64 -boot c -m 2048 -smp 2 -qmp stdio -vnc :7 -name secondary -enable-kvm -cpu qemu64,+kvmclock -device piix3-usb-uhci -drive if=none,id=colo-disk0,file.filename=/ubuntu-vm/ubuntu-cheng.img,driver=raw,node-name=node0 -drive if=virtio,id=active-disk0,driver=replication,mode=secondary,file.driver=qcow2,top-id=active-disk0,file.file.filename=/ubuntu-vm/active_disk.img,file.backing.driver=qcow2,file.backing.file.filename=/ubuntu-vm/hidden_disk.img,file.backing.backing=colo-disk0 -netdev tap,id=hn0,vhost=off,script=/etc/qemu-ifup,downscript=/etc/qemu-ifdown -device e1000,netdev=hn0,mac=52:a4:00:12:78:66 -incoming rdma:10.22.1.3:8888 -chardev socket,id=red0,path=/dev/shm/mirror.sock -chardev socket,id=red1,path=/dev/shm/redirector.sock -object filter-redirector,id=f1,netdev=hn0,queue=tx,indev=red0 -object filter-redirector,id=f2,netdev=hn0,queue=rx,outdev=red1

On major backup VM's monitor, issue command
{'execute':'qmp_capabilities'}
{ 'execute': 'nbd-server-start',
  'arguments': {'addr': {'type': 'inet', 'data': {'host': '10.22.1.3', 'port': '8889'} } }
}
{'execute': 'nbd-server-add', 'arguments': {'device': 'colo-disk0', 'writable': true } }

On leader VM's monitor, issue command:
{'execute':'qmp_capabilities'}
{ 'execute': 'human-monitor-command',
  'arguments': {'command-line': 'drive_add -n buddy driver=replication,mode=primary,file.driver=nbd,file.host=10.22.1.3,file.port=8889,file.export=colo-disk0,node-name=node0'}}
{ 'execute':'x-blockdev-change', 'arguments':{'parent': 'colo-disk0', 'node': 'node0' } }
{ 'execute': 'migrate-set-capabilities',
      'arguments': {'capabilities': [ {'capability': 'x-colo', 'state': true } ] } }
{ 'execute': 'migrate', 'arguments': {'uri': 'rdma:10.22.1.3:8888' } }


Normal startup:
x86_64-softmmu/qemu-system-x86_64 /local/ubuntu/ubuntu-singa.img -m 2048 -smp 2 -enable-kvm -cpu qemu64,+kvmclock -netdev tap,id=hn0,script=/etc/qemu-ifup,downscript=/etc/qemu-ifown -device e1000,id=e0,netdev=hn0,mac=52:a4:00:12:78:66 -vnc :7