#!/bin/bash

#xl170

echo "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDXPY6VzIvivxGOlFifJlbsbWYnwIDbLdcZAklivcJzlizdCNKHWZF2fNqF3/GFMtJkT2v5E5/IxxmWzN2Lm+3YYYI1mEbwL9LuwFncuhxgqhP8HT81Z8bXKShEyMDK6lii11eJ9xFKwuv4We4a5A+jTEu4HROsO1en64e7JcHr502lphVFzljiduwsFjFpmqXc6Y8rQdMPn4guQokcPwI7/8xMO72q8v+qsGvi7ss2ed8p5fyZfe2xuSqT2a6aoFNdS2r0/1xIFEM1B1jXwoxD3nrq3/d3OvTKcvt56Y+uVO+TfOb3lzMzSQMN40zmCfD4tJg7WxYKuezzFpDG5SyRdUHmelp7lYiprPySfSFmZNRQ56ClhKpIDwMVWNf9MqXi1ztU8AlrfHuXCvqyrlStrmNfqGzzv7p5uKqppfNhneanMJLpwuAPxFkynrlOT02tj+nIEeKzoqoarZmNEk73tNQ7tUypEQLPcGxUi+Iyapy/zh6sE25WSdh3heSsxtc= X1aoyang@node0.x1aoyang-255734.adslremotememory-pg0.utah.cloudlab.us" >> ~/.ssh/authorized_keys

sudo apt update
sudo apt install -y cmake libboost-all-dev libgoogle-perftools-dev libtbb-dev libgtest-dev libgflags-dev libgoogle-glog-dev

git clone https://github.com/memkind/memkind.git
cd memkind
./autogen.sh
./configure --prefix=/usr
make
sudo make install
cd ..; rm -rf memkind

sudo apt-get install lsb-release curl gpg
[ ! -f "/usr/share/keyrings/redis-archive-keyring.gpg" ] && curl -fsSL https://packages.redis.io/gpg | sudo gpg --dearmor -o /usr/share/keyrings/redis-archive-keyring.gpg
sudo chmod 644 /usr/share/keyrings/redis-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/redis-archive-keyring.gpg] https://packages.redis.io/deb $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/redis.list
sudo apt-get update
sudo apt-get install -y redis

wget https://github.com/redis/hiredis/archive/master.zip
unzip master.zip
cd hiredis-master
make; sudo make install
sudo mkdir /usr/lib/hiredis
sudo cp /usr/local/lib/libhiredis.so.1.3.0 /usr/lib/hiredis/
sudo mkdir /usr/include/hiredis
sudo cp /usr/local/include/hiredis/hiredis.h /usr/include/hiredis/
sudo ldconfig
cd ..; rm -rf hiredis-master; rm -rf master.zip

wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-7.1.0.0/MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu20.04-x86_64.tgz
tar -zxvf MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu20.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu20.04-x86_64
sudo ./mlnxofedinstall --force
cd ../; sudo rm -rf MLNX_OFED_LINUX-4.9-7.1.0.0-ubuntu20.04-x86_64*

sudo sed -i "s/GRUB_CMDLINE_LINUX_DEFAULT=\"/GRUB_CMDLINE_LINUX_DEFAULT=\"iommu=pt/" /etc/default/grub
sudo grub-mkconfig -o /boot/grub/grub.cfg
sudo /etc/init.d/openibd restart

sudo mst start
sudo mlxconfig -y -d /dev/mst/mt4117_pciconf0 set SRIOV_EN=1 NUM_OF_VFS=16
sudo mlxconfig -y -d /dev/mst/mt4117_pciconf1 set SRIOV_EN=1 NUM_OF_VFS=16

sudo reboot
