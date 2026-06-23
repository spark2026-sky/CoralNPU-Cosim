# CoralNPU Qemu Cosimulation

This project is for co-simulating the CoralNPU RTL with Qemu to assist NPU software development.
By co-simulating the CoralNPU RTL is exposed as a custom PCI-e device inside Qemu virtual machine,
so that the NPU software development can be done in the full fledged Linux environment.

This project is based on Xilinx open source LibSystemCTLM-SoC release, the unmodified files keep
its own license agreement, the new introduced or modified files are released under the GPL 2.0
license.

## Dependencies

You will need to install below tools
```
systemc-2.3.3
verilator
Qemu
```
## Build and install the tools

systemc-2.3.3
```
https://accellera.org/images/downloads/standards/systemc/systemc-2.3.3.tar.gz
install it to /usr/local/systemc-2.3.3
$ sudo mkdir /usr/local/systemc-2.3.3
$ cd systemc-2.3.3
$ configure --prefix=/usr/local/systemc-2.3.3
$ make
$ sudo make install
```
verilator
```
https://github.com/verilator/verilator
commit a2874b324a4f765bd4a49542f7f0913a4cf2521d is used for this release
Build and install Verilator in a specific directory and add the "bin" diretory to the PATH
```
Qemu
```
https://github.com/Xilinx/qemu
```
## Install Ubuntu virtual machine
```
$ wget https://cloud-images.ubuntu.com/releases/focal/release-20210125/ubuntu-20.04-server-cloudimg-amd64.img
$ wget https://cloud-images.ubuntu.com/releases/focal/release-20210125/unpacked/ubuntu-20.04-server-cloudimg-amd64-vmlinuz-generic
$ wget https://cloud-images.ubuntu.com/releases/focal/release-20210125/unpacked/ubuntu-20.04-server-cloudimg-amd64-initrd-generic
```

Resize the image to 30G.

```
$ qemu-img resize ~/Downloads/ubuntu-20.04-server-cloudimg-amd64.img 30G
```

Create a disk image with user-data to be used for starting the cloud
image.
```
$ sudo apt-get install cloud-image-utils
$ cd ~/Downloads
$ cat >user-data <<EOF
#cloud-config
password: pass
chpasswd: { expire: False }
ssh_pwauth: True
EOF
$ cloud-localds user-data.img user-data
```
## Build the simulator

```
cd CoralNPU-Cosim/soc/coralnpu/sim

make
OR
VM_TRACE=1 make
```

The latter enables tracing to dump VCD waveform files.

The simulator executable named "coralnpu-sim" would be generated in the current directory.

## Launch Ubuntu Virtual Machine

```
mkdir /tmp/machine-x86

QEMU=/path-to-qemu-installation/bin
UBUNTU=/path-to-ubuntu-installation

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$QEMU/lib64

$QEMU/qemu-system-x86_64 -M q35,accel=kvm,kernel-irqchip=split        \
	  -kernel $UBUNTU/ubuntu-20.04-server-cloudimg-amd64-vmlinuz-generic               \
    -append "root=/dev/sda1 rootwait console=tty1 console=ttyS0 intel_iommu=on"       \
    -initrd $UBUNTU/ubuntu-20.04-server-cloudimg-amd64-initrd-generic                \
       -drive file=$UBUNTU/ubuntu-20.04-server-cloudimg-amd64.img                       \
    -drive file=$UBUNTU/user-data.img,format=raw                                     \
    -device intel-iommu,intremap=on,device-iotlb=on         \
    -cpu qemu64 -smp 8 -m 8G                      \
    -netdev user,hostfwd=tcp:127.0.0.1:2225-10.0.2.15:22,id=n0  \
    -device virtio-net,netdev=n0                    \
    -machine-path /tmp/machine-x86/                  \
    -serial mon:stdio                       \
    -device ioh3420,id=rootport,slot=0              \
    -device ioh3420,id=rootport1,slot=1
```

## Start co-simulation

The Ubuntu VM boots normally and stops at login console.
In the console type "Ctrl-a c", which is type 'a' while holding 'Ctrl' key and release both keys,
and then type 'c' key, which will bring you to the Qemu monitor console with "(qemu)" indicator.

On the console initiate the following command

```
(qemu) device_add remote-port-pci-adaptor,bus=rootport1,id=rp0
```

We expect to see something like the following:
```
Failed to connect socket machine-x86//qemu-rport-_machine_peripheral_rp0_rp: Connection refused
info: QEMU waiting for connection on: disconnected:unix:machine-x86//qemu-rport-_machine_peripheral_rp0_rp,server
```

On another terminal initiate the CoralNPU simulator

```
LD_LIBRARY_PATH=/usr/local/systemc-2.3.3/lib-linux64/ ./coralnpu-sim unix:/tmp/machine-x86/qemu-rport-_machine_peripheral_rp0_rp 10000
```

We expect to see something like the following:
```
        SystemC 2.3.3-Accellera --- May 12 2026 15:15:49
        Copyright (c) 1996-2018 by all Contributors,
        ALL RIGHTS RESERVED

Info: (I702) default timescale unit used for tracing: 1 ps (./coralnpu-sim.vcd)
connect to /tmp/machine-x86/qemu-rport-_machine_peripheral_rp0_rp
```

Then switch back to the Ubuntu Qemu console and initiate following command:
```
device_add remote-port-pci-device,bus=rootport,rp-adaptor0=rp,rp-chan0=0,vendor-id=0x10ee,device-id=0xd004,class-id=0x0700,revision=0x12,nr-io-bars=0,nr-mm-bars=1,bar-size0=0x100000,id=pcidev1
```

On the third terminal login to the Ubuntu VM by username 'ubuntu' and password 'pass':

```
ssh -p 2225 ubuntu@localhost
```

## Build and run the CoralNPU driver

Build the driver inside Ubuntu VM

```
git clone https://github.com/spark2026-sky/CoralNPU-driver
cd CoralNPU-driver
make
```

Build the CoralNPU application

In the coralnpu directory
```
bazel build //examples:coralnpu_v2_hello_world_add_floats
```

Then copy the generated elf file
```
coralnpu/k8-fastbuild-ST-dd8dc713f32d/bin/examples/coralnpu_v2_hello_world_add_floats.elf
```
to
```
nfp.elf
```
in the current directory


Run the driver

```
sudo modprobe vfio-pci nointxmask=1
sudo sh -c 'echo 10ee d004 > /sys/bus/pci/drivers/vfio-pci/new_id'
sudo ./npu-driver  0000:01:00.0 3
```

## Check the simulation waveform

Use the coralnpu_v2_hello_world_add_floats.elf as an example

The co-simulation would generate 2 VCD files in the "CoralNPU-Cosim/soc/coralnpu/sim" directory,
"coralnpu-sim.vcd" and "npu.vcd" respectively.

```
gtkwave npu.vcd
```

Trace the following signals to make sure the NPU application succeed to run to the end.
top
  --> npu_core
    --> npu
      --> CoralNPU_Top
        --> CoreMiniAxi_inst
          --> core
            --> score
              --> fetch

Signals:
clock
reset
pc
io_inst_lanes_0_bits_addr[31:0]
io_inst_lanes_0_bits_inst[31:0]

Signal values at the end of the simulation
pc[31:0]                        == 00000134 00000138 0000013C 00000140
io_inst_lanes_0_bits_addr[31:0] == 00000134 00000138 0000013C00000140
io_inst_lanes_0_bits_inst[31:0] == B0202573 B82025F3 08000073 0000006F

Dump the content of the ELF file for cross checking
riscv32-gnu-toolchain/bin/riscv32-unknown-elf-objdump -d npu.elf
00000134 <success>:
 134:   b0202573                csrr    a0,minstret
 138:   b82025f3                csrr    a1,minstreth
 13c:   08000073                .4byte  0x8000073

00000140 <loop>:
 140:   0000006f                j       140 <loop>

## Known issues

Some times compilation of verilated c++ code process would fail because of generated signals have
c++ mangled names.

A simple workaround is clean and re-make.
