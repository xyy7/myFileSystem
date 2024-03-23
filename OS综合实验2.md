# OS综合实验2

## 实验过程

实验就是了解布局，然后写数据，来达到理解的目的。

EXT2本质是一种文件系统。

## 组织格式：

元数据区：管理结构

数据区：文件数据

还有特殊的结构：启动扇区

==》形成块组

然后依次介绍了一下管理结构的具体细节：

超级块、块组描述符、数据盘快位图、索引节点盘块位图、索引节点表、数据盘块

## EXT2文件系统的创建

### 7.2.1. 分配磁盘空间

 首先，创建一个大小为 512MB 的文件，后面再将它变成环回（loopback）设备并在其上创 建文件系统。

```
dd if=/dev/zero of=bean bs=1k count=512000

ll -h bean
```

### 7.2.2. 创建环回设备 

通过 losetup 将制定文件设置为环回设备，从而满足我们创建文件系统需要“块设备”的 基本需求。

```
 losetup /dev/loop0 bean

 cat /proc/partitions 
```

### 7.2.3. 创建 EXT2 文件系统 

然后使用 mke2fs 命令在该设备上建立 EXT2 文件系统。

```
mke2fs /dev/loop0
```



### 7.2.4. 挂载文件系统 

首先创建挂载点（目录，该目录会被挂载对象所覆盖）

```
mkdir /mnt/bean

mount -t ext2 /dev/loop0 /mnt/bean

mount

cd /mnt/bean/
```

### 7.3.1. 布局信息

```
 dumpe2fs /dev/loop0

dd if=/dev/loop0 bs=1k count=261 |od -tx1 -Ax > /tmp/dump_sp_hex

cat /tmp/dump_sp_hex  或者用vi指令


```

### 7.3.2. 块组描述符

cat /tmp/dump_sp_hex  或者用vi指令

### 7.3.3. 索引节点与文件内容

 cp /home/gen/apue/code/ext2/2 ./

 vim 1.txt

ls -l

ls –i

【有事要注意是在那个文件夹下面操作的】

dd if=/dev/loop0 bs=1k count=2042 |od -tx1 -Ax > /tmp/dump_sp_hex0

### 7.3.4. 目录结构

首先，我们在 bean 目录下创建目录 directory，

 ll

然后再在 directory 目录下创建 3 个普通文件（regular file，使用 touch 命令完成）

ll

接下来，我们试着再在目录 directory 下面创建管道文件（用 mkfifo 命令）、目录文件、 节点文件（用 mknod 命令）

ll





文件系统《==磁盘分区>=柱面>扇区

随着技术的发展：一个可被挂载的数据为一个文件系统

数据（block）+管理（inode）==》块（超级块（在多个块组中都有备份））==》块组==》EXT2文件系统

###  VFS 虚拟文件系统

1.创建一个100M的文件或者创建一个100M的共享内存

![image-20210607224456439](C:\Users\xuyuyu5207\AppData\Roaming\Typora\typora-user-images\image-20210607224456439.png)

2.尝试自行设计一个C语言小程序，使用步骤1分配的100M空间（共享内存或mmap），然后假设这100M空间为一个空白磁盘，设计一个简单的文件系统管理这个空白磁盘，给出文件和目录管理的基本数据结构，并画出文件系统基本结构图，以及基本操作接口。（20分）

3.在步骤1的基础上实现部分文件操作接口操作，创建目录mkdir，删除目录rmdir，修改名称，创建文件open，修改文件，删除文件rm，查看文件系统目录结构ls。（30分）

4.参考进程同步的相关章节，通过信号量机制实现多个终端对上述文件系统的互斥访问，系统中的一个文件允许多个进程读，不允许写操作；或者只允许一个写操作，不允许读。（20分）

5.实验报告书写质量（30分）

