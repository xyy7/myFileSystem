#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<iostream>
#include<sys/shm.h>

using namespace std;

const int KB=1024;
const int MB=1024*KB;
const int DISK_SIZE=100*MB;

//假设：
//1.一个文件只能放在一个块组中
//2.一个块1KB
//3.bitmap inode diritem大小由自己设计 

//难点：
//1.操作的是内存，根据偏移来进行计算;
//2.创建和删除文件，需要同时修改的东西很多 

//功能：
//1.实现当前目录下的基本文件操作:pwd cd ls mkdir rmdir mv touch rmdir open read write 等 

//不足
//1.文件大小固定 
//2.没有实现跨组和多级索引
//3. 仅支持单级目录


const int PER_DIR_ITEM=32;
const int MAX_LEN=1024/PER_DIR_ITEM;

const int PER_INODE=64;
const int PER_BLOCK=1*KB;
const int PER_GROUP=1*MB;	//如果采用char或者bit作为位图的话，一个块装载1024B==>1024块的信息==》1MB的信息

const int GROUP_NUM=DISK_SIZE/PER_GROUP;  //100*1024*1024   /  1024*1024=100
const int GROUP_BLOCK_NUM=1024;
const int GROUP_INODE_NUM=16;

const int ROOT_BLOCK=5;
const int KEY=0615;

//1024-1-100-1-1-1:1个超级块，100个group块，1个blockmap，1个inodemap，1个inodetable，其他为数据块


//超级块存储全局信息，每个块组需要有自己的块组描述块，需要在各个块组进行备份
//占用1块   
struct SuperBlock{
	int inodesCount;    //inode数量
	int blocksCount;	//block数量
	int rblocksCount;   //保存的block数量
	int freeBlockCount; //空闲的block数量
	int freeInodeCount; //空闲的inode数量
	int blocksPerGroup; //每个块组的block数量
};

//每个块组占用1块
struct GroupBlock{
	int blockMapNum;       //blockmap块所在盘块
	int inodeMapNum;  	   //inodeMap块所在盘块
	int inodeTableNum;     //索引表起点盘块 
	
	int freeBlockCount;	   //该块组中空闲block的数量
	int freeInodeCount;    //该块组中空闲的inode的数量
	int dirNodeCount;	   //该块组中目录的数量
	//int pad;
};



struct Inode{
	int len;	    //文件实际内容末尾(块长) 或者 目录项的长度
	int linkCount;  //用于创建硬链接,本系统暂不支持此功能
	int size;     //文件的大小，以盘块为单位,默认为1
	int block[13];  //存储数据，可以采用多级索引，本系统暂不支持此功能
};


struct DirItem{
	int inodeNum;	//该文件的inode编号
	int type;       //有一些文件类型不需要数据文件，只需要目录项和iNode  0代表文件，1代表文件夹
	char name[24];  //不超过24个字符
};


//仅支持连续存储和连续删除
//创建文件需要输入大小，且一旦创建不可更改
class Disk{
private:
	int size;
	int numOfGroup;
	SuperBlock *sp;   //100个超级块      只是作为一种访问形式【因为没有特殊性】
	GroupBlock *gb;   //100*100个group块
	int shm_id;
	void *shm_buf=(void*)0;
	int curDirBlock;  //当前目录所在盘块
    int curDirInode;  //当前目录所在Inode
	char curName[24]; //当前目录名称

public:
	Disk(){
		//默认创建100MB
		this->size=DISK_SIZE;
		shm_id=shmget((key_t)KEY,DISK_SIZE,0666|IPC_CREAT);
		//将共享内存映射到进程空间
		shm_buf = shmat(shm_id, 0, 0);
		numOfGroup=GROUP_NUM;
		init();
		cout<<"Make a 100MB disk successfully!\n";
	}
	Disk(int size){
		this->size=size;
		//创建共享内存
		shm_id=shmget((key_t)KEY,size,0666|IPC_CREAT);
		//将共享内存映射到进程空间
		numOfGroup=size/PER_GROUP;
		shm_buf = shmat(shm_id, 0, 0);
		init();
		cout<<"Make a "<<size<<"MB disk successfully!\n";
		
	};
	~Disk(){
		//解除映射
		shmdt(shm_buf);
		//删除共享内存
		//shmctl(shmid,IPC_RMID,0);
		if(shmctl(shm_id,IPC_RMID,0)==-1)
			printf("Delete error.\n");
		cout<<"Delete a disk successfully!\n";
	};
	
	int getBlockNum(void *addr){return ((char*)addr-(char *)shm_buf)/PER_BLOCK;};
	void* getBlockAddr(int num){return shm_buf+num*PER_BLOCK;};
	Inode* getIndoeAddr(int num){
		//GROUP_INODE_NUM
		int group=num/GROUP_INODE_NUM;
		int offset=num%GROUP_INODE_NUM;
		return (Inode*)(getInodeTableAddr(group)+offset*sizeof(Inode));
	}
	int getInodeNum(Inode* addr){
		int group=getGroup(addr);
		int offset=(int)((char*)addr-(char*)shm_buf)%PER_GROUP;
		return group*GROUP_INODE_NUM+(offset-PER_BLOCK*(1+numOfGroup+1+1))/sizeof(Inode);
	}
	void *getBlockMapAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup)*PER_BLOCK;
	}
	void *getInodeMapAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup+1)*PER_BLOCK;
	}
	void *getInodeTableAddr(int group){
		return (char*)shm_buf+group*PER_GROUP+(1+numOfGroup+1+1)*PER_BLOCK;
	}
	int getGroup(void *addr){
		return (int)((char*)addr-(char*)shm_buf)/PER_GROUP;
	}
	void *getGroupBlockAddr(int group){
		return (char*)shm_buf+sizeof(SuperBlock)+group*sizeof(GroupBlock);
	}
	
	//判断磁盘是否有足够的空间
	bool isDiskSpaceEnough(int size){
		SuperBlock* sp=(SuperBlock*)shm_buf;
		return sp->freeBlockCount>size&&sp->freeInodeCount>0;
	}
	
	//目录默认是足够的
	bool isDirSpaceEnough(){
		//pass
		return true;
	}
	
	//仅实现单独获取block【同时进行更新】
	int getFreeBlock(){
		for(int i=0;i<numOfGroup;++i){
			GroupBlock* gb=(GroupBlock*)getGroupBlockAddr(i);
			//if(gb->freeBlockCount){ //凭借这个判断会有延迟
				char* blockMap=(char*)getBlockMapAddr(i);
				for(int j=0;j<GROUP_BLOCK_NUM;++j){
					if(blockMap[j]=='0'){
						blockMap[j]='1';
						return i*GROUP_BLOCK_NUM+j;
					}
				}
			//}
		}
		return -1; 
	}; 
	
	//获取inode【同时进行更新】
	int getFreeInode(){
		
		for(int i=0;i<numOfGroup;++i){
			GroupBlock* gb=(GroupBlock*)getGroupBlockAddr(i);
			if(gb->freeInodeCount){
				char* inodeMap=(char*)getInodeMapAddr(i);
				for(int j=0;j<GROUP_INODE_NUM;++j){
					if(inodeMap[j]=='0'){
						inodeMap[j]='1';
						return i*GROUP_INODE_NUM+j;
					}
				}
			}
		}
	};
	
	
	//清除inodeMap
	void releaseInodeMap(int inodeNum){
		int group=inodeNum/GROUP_INODE_NUM;
		int offset=inodeNum%GROUP_INODE_NUM;
		char* map=(char*) getInodeMapAddr(group);
		map[offset]='0';
		
	};
		
	//清除blockMap
	void releaseBlockMap(int inodeNum){
		Inode* inode=(Inode*) getIndoeAddr(inodeNum);
		
		for(int i=0;i<inode->len;++i){
			int group=inode->block[i]/GROUP_BLOCK_NUM;
			int offset=inode->block[i]/GROUP_BLOCK_NUM;
			char* map=(char*) getBlockMapAddr(group);
			map[offset]='0';	
		}
	};
	
	//更新超级块和组块              type:1 创建 -1删除
	void updateSpAndBg(int size,int group,int type=1){
		for(int i=0;i<numOfGroup;++i){
			
			//更新超级块
			SuperBlock* sp=(SuperBlock*)(shm_buf+i*PER_GROUP);
			sp->freeBlockCount-=size*type;		//todo:可能会不同组，这里需要判断 
			sp->freeInodeCount-=1*type;
			
			//更新组块
			GroupBlock* gb=(GroupBlock*)(sp+group*sizeof(GroupBlock));
			gb->freeBlockCount-=size*type;
		    gb->freeInodeCount-=1*type;	
		}
	}
	
	
	//仅支持当前目录,假设不会越界
	void *getFreeDir(){
		//根据长度，返回地址
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		//顺便更新父级目录长度
		return getBlockAddr(curDirBlock)+cur->len*sizeof(DirItem);//实际上需要进行越界处理
	}
	

	
	void init(){
	
		//创建或者备份超级块
		for(int i=0;i<numOfGroup;i++){
			
			sp=(SuperBlock *)(shm_buf+i*PER_GROUP);
			sp->inodesCount=PER_BLOCK/PER_INODE*numOfGroup;
			sp->blocksCount=size/PER_BLOCK;
			 //减去超级块、组块、两个bitmap、一个inodeTable、减去根目录
			sp->freeBlockCount=sp->blocksCount-(1+numOfGroup+3)*numOfGroup-1; 
			sp->freeInodeCount=sp->inodesCount-1; //减去根目录
			sp->blocksPerGroup=PER_GROUP/PER_BLOCK;
			
		}
		
		//创建或者备份组块
		for(int i=0;i<numOfGroup;i++){
			for(int j=0;j<numOfGroup;++j){
		
				gb=(GroupBlock *)(shm_buf+i*PER_GROUP+
							sizeof(SuperBlock)+j*sizeof(GroupBlock));					
				gb->blockMapNum=getBlockNum((shm_buf+i*
							PER_GROUP+sizeof(SuperBlock)+numOfGroup*sizeof(GroupBlock)));
				//cout<<gb->blockMapNum<<endl;
				gb->inodeMapNum=gb->blockMapNum+1;
				gb->inodeTableNum=gb->inodeMapNum+1;
				gb->freeBlockCount=1024-1-100-1-1-1;
				gb->freeInodeCount=PER_BLOCK/PER_INODE;    
			}
		}

		//初始化各组块的map
		for(int i=0;i<numOfGroup;++i){
			//inodeMap全为0
			char* inodeMap=(char*)getInodeMapAddr(i);
			for(int j=0;j<GROUP_INODE_NUM;++j){
				inodeMap[j]='0';
			}
		
			//blockMap占据了1+numOfGroup+1+1+1
			char *blockMap=(char*)getBlockMapAddr(i);
			
			for(int j=0;j<GROUP_BLOCK_NUM;++j){
				if(j<1+numOfGroup+1+1+1)
					blockMap[j]='1';
				else
					blockMap[j]='0';
			}		
		}
		
		//创建根目录【inode编号和block编号不会重新开始计算】

		int blockNum=-1;
		int inodeNum=-1;
		
		if(isDiskSpaceEnough(1)){
			//1.刷新第0个组块的Map
			//刷新inodeMap

			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);//3.更新inodeTable
			
			//刷新blockmap
			inode->block[0]=ROOT_BLOCK;
			
			//2.刷新第0个组块的free inode、block、以及超级块的
			updateSpAndBg(1,0);
			
			//4.创建目录项
	
			DirItem* item=(DirItem *)getBlockAddr(ROOT_BLOCK);//1024+1024+1024...
			item->inodeNum=0;//16+16+16+16....
			item->type=1;
			strcpy(item->name,"."); //没有.. 说明了是根目录
			
			inode->len=1;
			inode->size=1;

		
		}else{
			cout<<"Disk space is not enough."<<endl;
		}
		
		curDirBlock=ROOT_BLOCK;
		curDirInode=0;
		strcpy(curName,"/"); //要保证curname有空间 
	}
	
	
	//根据路径查询block或者物理地址
	int path2block(char* path){
		//pass
	}
	
	void pwd(){
		cout<<"The cur dir is :"<<curName<<endl;
	}
	
	//仅支持当前目录下
	void cd(char *name){
		
		//查找block到最后一项,如果找到:替换
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			cout<<name<<endl;
			cout<<tmp->name<<endl;
			if(strcmp(name,tmp->name)==0&&tmp->type==1){
				//更换curDirInode和curDirBlock
				curDirInode=tmp->inodeNum;
				cur=getIndoeAddr(curDirInode);
				curDirBlock=cur->block[0];
				strcpy(curName,name);
				cout<<"cd successfully:"<<name<<endl;
				return ;
			}
		}
		
		//如果找不到:输出错误信息
		cout<<"Error: not found\n"<<endl;
		
	}
	
	//查找文件（默认）或者目录是否存在
	void* isExist(char* name,int type,int inode){
		//找到dirItem对应的位置
		Inode* cur=getIndoeAddr(inode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==type){	
				return tmp;
			}
		}
		return NULL;	
	}
	
	void* isExist(char* name){
		//找到dirItem对应的位置
		Inode* cur=getIndoeAddr(curDirInode);//是否直接记录地址效率更高？
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			//cout<<":"<<name<<endl;
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			//cout<<":"<<tmp->name<<endl;
			if(strcmp(name,tmp->name)==0&&tmp->type==0){	
				return tmp;
			}
		}
		return NULL;
	}
	
	void* isExist(char* name,int type){
		//找到dirItem对应的位置
		Inode* cur=getIndoeAddr(curDirInode);//是否直接记录地址效率更高？
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(cur->block[0])+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==type){	
				return tmp;
			}
		}
		return NULL;
		
	}
	
void mkdir(char *name){
		
		if(isExist(name,1)){
			cout<<"The file has existed"<<endl;
		}
		//绝对路径下寻找
		//pass
		
		
		//当前路径下查找
		
		//分配一个目录项 【因为目录和文件共用Inode，所以可以存放很多，但默认只占一个block】
		//分配一个inode 更新inodemap  更新组的inodecount 更新超级块的inodecount
		//分配一个block 
		//在block中填入: .. 父亲的inode type 文件
		int blockNum=-1;
		int inodeNum=-1;
		
		if(isDiskSpaceEnough(1)){
			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);
			inode->size=1;
			blockNum=getFreeBlock();
			inode->block[0]=blockNum;
			
			int group=getGroup(inode);
			updateSpAndBg(1,group);
			
			DirItem* item=(DirItem*)getFreeDir();
			Inode* cur=(Inode*)getIndoeAddr(curDirInode);
			cur->len++;
			
			item->inodeNum=inodeNum;
			item->type=1;
			strcpy(item->name,name);
			
			inode->len=2;
			item=(DirItem*)getBlockAddr(blockNum);
			strcpy(item->name,".");
			item->inodeNum=inodeNum;
			item->type=1;
			
			item=(DirItem*)(getBlockAddr(blockNum)+sizeof(DirItem));
			strcpy(item->name,"..");
			item->inodeNum=curDirInode;
			item->type=1;
			
			//cout<<getBlockAddr(blockNum)<<" "<<sizeof(DirItem)<<(void* )item<<endl;
			
		}else{
			cout<<"Disk space is not enough."<<endl;
		}
		
		
		
	}
	
	void rmdir(char* name){
		rmdir(name,curDirInode);
	}
	
	
	
	void rmdir(char *name,int fatherNode){
		//绝对路径下寻找
		//pass
		
		
		//在当前路径下查找
		DirItem* dir=(DirItem*)isExist(name,1,fatherNode);
		if(dir==NULL){
			cout<<"not found"<<endl;
			return;
		}
		
		//需要先递归删除目录中所有的东西
		//分配一个inode  逆过程
		//分配一个block  逆过程
		//分配一个目录项 删除目录项
		//在block中填入: .. 父亲的inode type 文件	//后面的可以不用管 这也就是为什么，如果没有覆盖能够还原的原因
		
		
		//文件夹里面的内容需要递归删除
		Inode* inode=(Inode*)getIndoeAddr(dir->inodeNum);
		if(inode->len>2){
			//遇到. ..不需要删除		
			
			for(int i=2;i<inode->len;++i){
				DirItem* item=(DirItem*)(getBlockAddr(inode->block[0])+i*sizeof(DirItem));
				//遇到文件夹递归删除
				if(item->type==1){
					rmdir(item->name,dir->inodeNum);
				}
				//遇到文件，直接删除
				else{
					rmfile(item->name,dir->inodeNum);
				}
			}		
		}
			
		//文件夹本身可以像个文件一样删除
		dir->type=0;
		rmfile(dir->name);
		
	}
	
    //这个最简单
	void mv(char *old_name,char *new_name){
		
		//查找block到最后一项,如果找到:替换
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			
			if(strcmp(old_name,tmp->name)==0){
				strcpy(tmp->name,new_name);
				cout<<"Rename successfully!"<<endl;
				return ;
			}
		}
		
		//如果找不到:输出错误信息
		cout<<"Error: not found\n"<<endl;
			
	}

	
	void touch(char *name){
		int sizeOfFile=5;
		//如果是绝对路径
		//pass
		
		//如果只是当前目录
		//查看是否已经存在
		if(isExist(name)){
			cout<<"File has Existed"<<endl;
			return;
		}
		

		int blockNum=-1;
		int inodeNum=-1;
		
		//cout<<"没有实现多级索引，size<10"<<endl;
		if(isDiskSpaceEnough(sizeOfFile)){
			//1.刷新第0个组块的Map
			//刷新inodeMap
			inodeNum=getFreeInode();
			Inode* inode=(Inode*)getIndoeAddr(inodeNum);//3.更新inodeTable
			//刷新blockmap
			for(int i=0;i<sizeOfFile;++i){
				blockNum=getFreeBlock();
				inode->block[i]=blockNum;
			}
			
			//2.刷新第0个组块的free inode、block、以及超级块的
			//updateSpAndBg(size,group)
			int group=getGroup(inode);
			updateSpAndBg(sizeOfFile,group);
			
			//4.创建目录项
			DirItem* item=(DirItem*)getFreeDir();
			Inode* cur=(Inode*)getIndoeAddr(curDirInode);
			cur->len++;
			item->inodeNum=inodeNum;//16+16+16+16....
			item->type=0;
			strcpy(item->name,name);
			
			
			inode->len=0;
			inode->size=sizeOfFile;
		
			cout<<"Make successfully:"<<name<<endl;
		
		}else{
			cout<<"Disk space is not enough"<<endl;
		}	
		return ;

	}
	
	void rmfile(char *name,int fatherNode){
		
		DirItem* fileItem=(DirItem*)isExist(name,0,fatherNode);
		if(fileItem==NULL){
			cout<<"Error: not found "<<name<<endl;
			return ;
		}
		//如果是绝对路径
		//pass
		
		
		//如果只是当前目录
		//删除diritem、位图、组块、超级块    inodetable 不用清除 内容不用清除，//可被申请  可被覆盖即可 目录里面看不见即可
		
		
		int inodeNum=fileItem->inodeNum;
		int group=inodeNum/GROUP_INODE_NUM;
		//清除inodeMap
		releaseInodeMap(inodeNum);
		
		//清除blockMap
		releaseBlockMap(inodeNum);
		
		//组块、超级块 free增加
		Inode* file=(Inode*)getIndoeAddr(inodeNum);
		updateSpAndBg(file->size,group,-1);
		
		//dirItem和最后的交换，然后curDirInode len--
		DirItem* last=(DirItem*)(getFreeDir()-sizeof(DirItem));
		fileItem->inodeNum=last->inodeNum;
		fileItem->type=last->type;
		strcpy(fileItem->name,last->name);
		
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		cur->len--;
		cout<<"delete "<<name<<" successfully"<<endl;
	}
	
	//重载
	void rmfile(char *name){
		
		DirItem* fileItem=(DirItem*)isExist(name,0);
		if(fileItem==NULL){
			cout<<"Error: not found "<<name<<endl;
			return ;
		}
		//如果是绝对路径
		//pass
		
		
		//如果只是当前目录
		//删除diritem、位图、组块、超级块    inodetable 不用清除 内容不用清除，//可被申请  可被覆盖即可 目录里面看不见即可
		
		
		int inodeNum=fileItem->inodeNum;
		int group=inodeNum/GROUP_INODE_NUM;
		//清除inodeMap
		releaseInodeMap(inodeNum);
		
		//清除blockMap
		releaseBlockMap(inodeNum);
		
		//组块、超级块 free增加
		Inode* file=(Inode*)getIndoeAddr(inodeNum);
		updateSpAndBg(file->size,group,-1);
		
		//dirItem和最后的交换，然后curDirInode len--
		DirItem* last=(DirItem*)(getFreeDir()-sizeof(DirItem));
		fileItem->inodeNum=last->inodeNum;
		fileItem->type=last->type;
		strcpy(fileItem->name,last->name);
		
		Inode* cur=(Inode*)getIndoeAddr(curDirInode);
		cur->len--;
		cout<<"delete "<<name<<" successfully"<<endl;
	}
	
	//仅支持当前目录下
	void* open(char *name){
		
		
		//找到dirItem对应的位置
		Inode* cur=getIndoeAddr(curDirInode);//是否直接记录地址效率更高？
		DirItem* tmp;
		for(int i=0;i<cur->len;++i){
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			if(strcmp(name,tmp->name)==0&&tmp->type==0){	
				cout<<"The file is open at"<<(void*)tmp<<endl;
				return getIndoeAddr(tmp->inodeNum);	//找到inode  //就意味着找到block
			}
		}
		
		//如果找不到:输出错误信息
		cout<<"Not found\n"<<endl;
		return NULL;
			
	}
	
	//仅支持当前目录下
	//因为有可能读到覆盖前的东西，所以需要注意结尾的地方
	void read(char *name){
		Inode* file=(Inode*)open(name);
		
		//找到不到文件
		if(file==NULL){
			cout<<"Not found\n"<<endl;
			return ;
		}
		
		//输出文件，遇到'\0'停止
		char* tmp;
		int cnt=0;
		for (int i=0;i<file->len;++i){
		    tmp=(char*)getBlockAddr(file->block[cnt++]);
			cout<<tmp;			
		}
		cout<<endl;
		cout<<"Read over\n";
		
	}
	
	//仅支持当前目录下,仅支持写入，不支持append
	void write(char *name,char *content){
		Inode* file=(Inode*)open(name);
		
		//找到不到文件
		if(file==NULL){
			cout<<"Not found\n"<<endl;
			return ;
		}
		
		
		//仅支持写入，不支持append
		//获取写入长度
		file->len=strlen(content)/1024;
		if((strlen(content)%1024!=0)){
			file->len++;
		}
		
		//写入到block中
		for(int i=0;i<file->len;++i){
			char *tmp=(char*)getBlockAddr(file->block[i]);
			if(i==file->len-1){
				strncpy(tmp,content+i*1024,strlen(content)%1024);
				break;
			}
			strncpy(tmp,content+i*1024,1024);
		}
		
		cout<<"Write over."<<endl;
			
	}
	
	//仅支持当前目录下
	void ls(){
		//首行
		printf("name\t\t type\t\t size\t\t inode\t\t content_size\t\t\n");
		
		//输出每一个目录项的内容:	
		Inode* cur=getIndoeAddr(curDirInode);
		DirItem* tmp;

		for(int i=0;i<cur->len;++i){
			//cout<<(getBlockAddr(curDirBlock)+sizeof(DirItem)*i)<<endl;
			tmp=(DirItem*)(getBlockAddr(curDirBlock)+sizeof(DirItem)*i);
			Inode* inode=getIndoeAddr(tmp->inodeNum);
			printf("%s\t\t %d\t\t %d\t\t %d\t\t %d\t\t\n",tmp->name,tmp->type,inode->size,tmp->inodeNum,inode->len);
		}	
	}
	
	
	
};


class FileSystem{
private:
	char* type="EXT2";
	Disk *disk;
public:
	FileSystem(){disk=new Disk();cout<<"create fileSystem...\n";}//需要注意的是初始化的阶段
	FileSystem(int diskSize){disk=new Disk(diskSize);cout<<"create fileSystem...\n";};
	void makeDir(char* path){cout<<"mkdir..."<<endl;disk->mkdir(path);};
	void delDir(char* dirPath){cout<<"deldir..."<<endl;disk->rmdir(dirPath);};
	void renameFile(char* path,char* new_name){cout<<"rename..."<<endl;disk->mv(path,new_name);};
	void openFile(char* path){cout<<"open..."<<endl;disk->open(path);};
	void touchFile(char* path){cout<<"touch..."<<endl;disk->touch(path);};
	void delFile(char* path){cout<<"delFile..."<<endl;disk->rmfile(path);};
	void showFile(){cout<<"ls..."<<endl;disk->ls();};
	void cdDir(char* name){cout<<"cd..."<<endl;disk->cd(name);}
	void write(char* name,char* content){cout<<"write..."<<endl;disk->write(name,content);}
	void read(char* name){cout<<"read..."<<endl;disk->read(name);}
	void pwd(){cout<<"pwd..."<<endl;disk->pwd();}
	
	
	~FileSystem(){delete disk;}
};





//shell0
int main(){
	//init
	FileSystem fileSystem=FileSystem();//MB为单位
	while(true){
		cout<<"input your op:"<<endl;
		string op;
		cin>>op;
		if(op=="mkdir"){
			char name[24];
			cin>>name;
			fileSystem.makeDir(name);
		}else if(op=="deldir"){
			char name[24];
			cin>>name;
			fileSystem.delDir(name);
			
		}else if(op=="mv"){
			char oldName[24],newName[24];
			cin>>oldName>>newName;
			fileSystem.renameFile(oldName,newName);
		}else if(op=="touch"){
			char name[24];
			cin>>name;
			fileSystem.touchFile(name);
		}else if(op=="ls"){
			fileSystem.showFile();
		}else if(op=="delfile"){
			char name[24];
			cin>>name;
			fileSystem.delFile(name);
			
		}else if(op=="open"){
			char name[24];
			cin>>name;
			fileSystem.openFile(name);
		    	
		}else if(op=="read"){
			char name[24];
			cin>>name;
			fileSystem.read(name);
			
		}else if(op=="write"){
			char name[24];
			char content[1024];
			cin>>name>>content;
			fileSystem.write(name,content);
			
		}else if(op=="cd"){
			char name[24];
			cin>>name;
			fileSystem.cdDir(name);
		}else if(op=="pwd"){
			fileSystem.pwd();
		}else if(op=="exit"){
			cout<<"exit system...\n";
			break;
		}	
	}	
}



